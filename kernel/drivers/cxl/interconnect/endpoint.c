// SPDX-License-Identifier: GPL-2.0
/*
 * CXL Fabric Interconnect - Endpoint Management
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/cxl/cxl_fabric.h>

/* Shim: Simulated endpoint memory pools */
struct cxl_endpoint_shim {
	void *memory_pool;
	size_t pool_size;
	struct mutex pool_lock;
};

/*
 * Endpoint sysfs attributes
 */
static ssize_t endpoint_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_endpoint *ep = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", ep->endpoint_id);
}
static DEVICE_ATTR_RO(endpoint_id);

static ssize_t total_memory_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cxl_endpoint *ep = dev_get_drvdata(dev);
	return sprintf(buf, "%llu\n", ep->total_memory);
}
static DEVICE_ATTR_RO(total_memory);

static ssize_t available_memory_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct cxl_endpoint *ep = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", atomic64_read(&ep->available_memory));
}
static DEVICE_ATTR_RO(available_memory);

static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cxl_endpoint *ep = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", ep->numa_node);
}
static DEVICE_ATTR_RO(numa_node);

static ssize_t supports_atomic_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cxl_endpoint *ep = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", ep->supports_atomic ? 1 : 0);
}
static DEVICE_ATTR_RO(supports_atomic);

static ssize_t supports_coherent_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct cxl_endpoint *ep = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", ep->supports_cache_coherent ? 1 : 0);
}
static DEVICE_ATTR_RO(supports_coherent);

static struct attribute *cxl_endpoint_attrs[] = {
	&dev_attr_endpoint_id.attr,
	&dev_attr_total_memory.attr,
	&dev_attr_available_memory.attr,
	&dev_attr_numa_node.attr,
	&dev_attr_supports_atomic.attr,
	&dev_attr_supports_coherent.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cxl_endpoint);

/*
 * Endpoint management
 */
struct cxl_endpoint *cxl_endpoint_create(struct cxl_fabric *fabric, u64 memory_size)
{
	struct cxl_endpoint *ep;
	struct cxl_endpoint_shim *shim;
	int ret;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return ERR_PTR(-ENOMEM);

	ep->fabric = fabric;
	ep->total_memory = memory_size;
	atomic64_set(&ep->available_memory, memory_size);

	INIT_LIST_HEAD(&ep->regions);
	init_rwsem(&ep->region_sem);

	/* Allocate simulated memory pool */
	shim = kzalloc(sizeof(*shim), GFP_KERNEL);
	if (!shim) {
		kfree(ep);
		return ERR_PTR(-ENOMEM);
	}

	shim->pool_size = memory_size;
	shim->memory_pool = vzalloc(memory_size);
	if (!shim->memory_pool) {
		pr_warn("CXL Endpoint: Cannot allocate %llu bytes, using smaller pool\n",
			memory_size);
		/* Try smaller allocation for testing */
		shim->pool_size = min_t(u64, memory_size, 256ULL * 1024 * 1024);
		shim->memory_pool = vzalloc(shim->pool_size);
		if (!shim->memory_pool) {
			kfree(shim);
			kfree(ep);
			return ERR_PTR(-ENOMEM);
		}
	}

	mutex_init(&shim->pool_lock);

	/* Store shim in endpoint (abuse access_mask for now) */
	ep->access_mask = (unsigned long)shim;

	/* Initialize device */
	device_initialize(&ep->dev);
	ep->dev.class = fabric->dev.class;
	ep->dev.parent = &fabric->dev;
	ep->dev.groups = cxl_endpoint_groups;
	dev_set_drvdata(&ep->dev, ep);

	/* Allocate endpoint ID */
	ret = ida_simple_get(&fabric->endpoint_ida, 0, CXL_MAX_ENDPOINTS, GFP_KERNEL);
	if (ret < 0) {
		vfree(shim->memory_pool);
		kfree(shim);
		kfree(ep);
		return ERR_PTR(ret);
	}
	ep->endpoint_id = ret;

	dev_set_name(&ep->dev, "endpoint%u", ep->endpoint_id);

	/* Initialize resource */
	ep->memory_resource.start = 0x1000000000ULL + (ep->endpoint_id * 0x10000000000ULL);
	ep->memory_resource.end = ep->memory_resource.start + memory_size - 1;
	ep->memory_resource.name = dev_name(&ep->dev);
	ep->memory_resource.flags = IORESOURCE_MEM;

	pr_debug("CXL Endpoint: Created endpoint%u with %llu bytes\n",
		 ep->endpoint_id, memory_size);

	return ep;
}
EXPORT_SYMBOL_GPL(cxl_endpoint_create);

void cxl_endpoint_destroy(struct cxl_endpoint *ep)
{
	struct cxl_endpoint_shim *shim;
	struct cxl_memnet_region *region, *tmp;

	if (!ep)
		return;

	pr_debug("CXL Endpoint: Destroying endpoint%u\n", ep->endpoint_id);

	/* Free all regions */
	down_write(&ep->region_sem);
	list_for_each_entry_safe(region, tmp, &ep->regions, ep_node) {
		list_del(&region->ep_node);
		kfree(region);
	}
	up_write(&ep->region_sem);

	/* Free simulated memory */
	shim = (struct cxl_endpoint_shim *)(unsigned long)ep->access_mask;
	if (shim) {
		vfree(shim->memory_pool);
		kfree(shim);
	}

	/* Remove from fabric if registered */
	if (device_is_registered(&ep->dev)) {
		device_unregister(&ep->dev);
	}

	/* Free resources */
	if (ep->fabric)
		ida_simple_remove(&ep->fabric->endpoint_ida, ep->endpoint_id);

	kfree(ep);
}
EXPORT_SYMBOL_GPL(cxl_endpoint_destroy);

int cxl_endpoint_add_to_fabric(struct cxl_fabric *fabric, struct cxl_endpoint *ep)
{
	int ret;

	mutex_lock(&fabric->fabric_lock);

	/* Register device */
	ret = device_add(&ep->dev);
	if (ret) {
		mutex_unlock(&fabric->fabric_lock);
		return ret;
	}

	/* Add to fabric list */
	list_add_tail(&ep->fabric_node, &fabric->endpoints);
	fabric->num_endpoints++;

	mutex_unlock(&fabric->fabric_lock);

	pr_info("CXL Endpoint: Added endpoint%u to fabric%u (%llu GB)\n",
		ep->endpoint_id, fabric->fabric_id, ep->total_memory / (1024*1024*1024));

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_endpoint_add_to_fabric);
