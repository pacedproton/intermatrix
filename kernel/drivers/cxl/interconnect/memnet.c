// SPDX-License-Identifier: GPL-2.0
/*
 * CXL Fabric Interconnect - Memory Network Interface
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/cxl/cxl_fabric.h>

/* For accessing endpoint shim memory */
struct cxl_endpoint_shim {
	void *memory_pool;
	size_t pool_size;
	struct mutex pool_lock;
};

static DEFINE_IDA(cxl_region_handle_ida);

/*
 * Memory region management
 */
struct cxl_memnet_region *cxl_memnet_alloc_remote(struct cxl_endpoint *ep,
						  size_t size, u32 flags)
{
	struct cxl_memnet_region *region;
	struct cxl_endpoint_shim *shim;
	u64 avail;
	int ret;

	if (!ep || size == 0)
		return ERR_PTR(-EINVAL);

	/* Check alignment */
	if (size & (PAGE_SIZE - 1))
		return ERR_PTR(-EINVAL);

	/* Check available memory */
	avail = atomic64_read(&ep->available_memory);
	if (avail < size) {
		pr_warn("CXL MemNet: Endpoint %u has insufficient memory (%llu < %zu)\n",
			ep->endpoint_id, avail, size);
		return ERR_PTR(-ENOMEM);
	}

	/* Allocate region structure */
	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return ERR_PTR(-ENOMEM);

	region->owner = ep;
	region->size = size;
	region->access_flags = flags;
	region->coherent = ep->supports_cache_coherent && (flags & CXL_MEM_COHERENT);

	/* Allocate handle */
	ret = ida_simple_get(&cxl_region_handle_ida, 1, INT_MAX, GFP_KERNEL);
	if (ret < 0) {
		kfree(region);
		return ERR_PTR(ret);
	}
	region->handle = ret;

	/* Get simulated memory from endpoint shim */
	shim = (struct cxl_endpoint_shim *)(unsigned long)ep->access_mask;
	if (!shim || !shim->memory_pool) {
		ida_simple_remove(&cxl_region_handle_ida, region->handle);
		kfree(region);
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&shim->pool_lock);

	/* Simulate physical address allocation */
	region->remote_phys_addr = ep->memory_resource.start +
				   (ep->total_memory - atomic64_read(&ep->available_memory));

	/* Map to local virtual address (in real HW, this would be ioremap) */
	region->local_virt_addr = shim->memory_pool +
				  (region->remote_phys_addr - ep->memory_resource.start);

	mutex_unlock(&shim->pool_lock);

	/* Set page protection */
	if (flags & CXL_MEM_WRITE)
		region->prot = PAGE_KERNEL;
	else
		region->prot = PAGE_KERNEL_RO;

	/* Initialize statistics */
	atomic64_set(&region->read_count, 0);
	atomic64_set(&region->write_count, 0);
	region->last_access = ktime_get();

	/* Add to endpoint's region list */
	down_write(&ep->region_sem);
	list_add(&region->ep_node, &ep->regions);
	up_write(&ep->region_sem);

	/* Update available memory */
	atomic64_sub(size, &ep->available_memory);

	/* Update fabric statistics */
	if (ep->fabric)
		atomic_inc(&ep->fabric->stats.active_regions);

	kref_init(&region->refcount);

	pr_debug("CXL MemNet: Allocated %zu bytes on endpoint%u (handle=0x%llx)\n",
		 size, ep->endpoint_id, region->handle);

	return region;
}
EXPORT_SYMBOL_GPL(cxl_memnet_alloc_remote);

static void cxl_memnet_region_release(struct kref *ref)
{
	struct cxl_memnet_region *region;

	region = container_of(ref, struct cxl_memnet_region, refcount);

	pr_debug("CXL MemNet: Releasing region handle=0x%llx\n", region->handle);

	/* Remove from endpoint */
	if (region->owner) {
		down_write(&region->owner->region_sem);
		list_del(&region->ep_node);
		up_write(&region->owner->region_sem);

		/* Return memory to endpoint */
		atomic64_add(region->size, &region->owner->available_memory);

		/* Update fabric statistics */
		if (region->owner->fabric)
			atomic_dec(&region->owner->fabric->stats.active_regions);
	}

	ida_simple_remove(&cxl_region_handle_ida, region->handle);
	kfree(region);
}

void cxl_memnet_free_remote(struct cxl_memnet_region *region)
{
	if (!region)
		return;

	kref_put(&region->refcount, cxl_memnet_region_release);
}
EXPORT_SYMBOL_GPL(cxl_memnet_free_remote);

/*
 * Zero-copy memory operations (SHIM implementation)
 */
int cxl_memnet_put(struct cxl_memnet_region *region,
		   const void *src, size_t len, loff_t offset)
{
	void *dest;

	if (!region || !src)
		return -EINVAL;

	/* Bounds check */
	if (offset + len > region->size)
		return -EINVAL;

	/* Permission check */
	if (!(region->access_flags & CXL_MEM_WRITE))
		return -EACCES;

	dest = (void *)region->local_virt_addr + offset;

	/* SHIM: Direct memcpy (real HW would use MMIO writes) */
	memcpy(dest, src, len);

	/* Memory barrier for non-coherent */
	if (!region->coherent)
		wmb();

	/* Update statistics */
	atomic64_inc(&region->write_count);
	region->last_access = ktime_get();

	if (region->owner && region->owner->fabric)
		atomic64_add(len, &region->owner->fabric->stats.total_bytes_transferred);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_memnet_put);

int cxl_memnet_get(struct cxl_memnet_region *region,
		   void *dest, size_t len, loff_t offset)
{
	const void *src;

	if (!region || !dest)
		return -EINVAL;

	/* Bounds check */
	if (offset + len > region->size)
		return -EINVAL;

	/* Permission check */
	if (!(region->access_flags & CXL_MEM_READ))
		return -EACCES;

	src = (const void *)region->local_virt_addr + offset;

	/* Memory barrier for non-coherent */
	if (!region->coherent)
		rmb();

	/* SHIM: Direct memcpy (real HW would use MMIO reads) */
	memcpy(dest, src, len);

	/* Update statistics */
	atomic64_inc(&region->read_count);
	region->last_access = ktime_get();

	if (region->owner && region->owner->fabric)
		atomic64_add(len, &region->owner->fabric->stats.total_bytes_transferred);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_memnet_get);

/*
 * Atomic operations (SHIM implementation)
 */
int cxl_memnet_atomic_add(struct cxl_memnet_region *region,
			  loff_t offset, s64 value)
{
	volatile s64 *addr;

	if (!region)
		return -EINVAL;

	if (offset & 7)  /* Must be 8-byte aligned */
		return -EINVAL;

	if (offset + sizeof(s64) > region->size)
		return -EINVAL;

	if (!(region->access_flags & CXL_MEM_ATOMIC))
		return -EACCES;

	addr = (volatile s64 *)((u8 *)region->local_virt_addr + offset);

	if (region->owner && region->owner->supports_atomic) {
		/* SHIM: Use atomic_add (real HW would use CXL atomic) */
		__atomic_add_fetch(addr, value, __ATOMIC_SEQ_CST);
	} else {
		/* Software fallback */
		s64 old, new;
		do {
			old = *addr;
			new = old + value;
		} while (__sync_val_compare_and_swap(addr, old, new) != old);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_memnet_atomic_add);

int cxl_memnet_compare_swap(struct cxl_memnet_region *region,
			    loff_t offset, u64 old_val, u64 new_val,
			    u64 *actual_old)
{
	volatile u64 *addr;
	u64 result;

	if (!region)
		return -EINVAL;

	if (offset & 7)
		return -EINVAL;

	if (offset + sizeof(u64) > region->size)
		return -EINVAL;

	if (!(region->access_flags & CXL_MEM_ATOMIC))
		return -EACCES;

	addr = (volatile u64 *)((u8 *)region->local_virt_addr + offset);

	/* SHIM: Use __sync_val_compare_and_swap */
	result = __sync_val_compare_and_swap(addr, old_val, new_val);

	if (actual_old)
		*actual_old = result;

	return (result == old_val) ? 0 : -EAGAIN;
}
EXPORT_SYMBOL_GPL(cxl_memnet_compare_swap);

/*
 * Module initialization
 */
static int __init cxl_memnet_init(void)
{
	pr_info("CXL Memory Network: Initialized\n");
	return 0;
}

static void __exit cxl_memnet_exit(void)
{
	pr_info("CXL Memory Network: Shutdown\n");
}

module_init(cxl_memnet_init);
module_exit(cxl_memnet_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("InterMatrix Project");
MODULE_DESCRIPTION("CXL Fabric Interconnect - Memory Network Interface");
