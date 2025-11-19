/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * libcxl - Memory Operations
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "libcxl/cxl.h"
#include "../internal.h"

/* Redeclare opaque types with their definitions */
struct cxl_context {
	int last_error;
	unsigned int ref_count;
	bool initialized;
};

struct cxl_fabric {
	struct cxl_context *ctx;
	int fabric_id;
	int fd;
	char dev_path[256];
	struct cxl_fabric_info info;
	bool info_valid;
	cxl_endpoint_t **endpoints;
	size_t num_endpoints;
	unsigned int ref_count;
};

struct cxl_endpoint {
	cxl_fabric_t *fabric;
	uint32_t endpoint_id;
	struct cxl_endpoint_info info;
	bool info_valid;
	unsigned int ref_count;
};

struct cxl_mem {
	cxl_endpoint_t *endpoint;
	size_t size;
	int access;
	uint64_t handle;
	void *mapped_addr;
	size_t mapped_size;
	uint64_t read_count;
	uint64_t write_count;
	unsigned int ref_count;

	/* SHIM: Direct access to simulated memory */
	int shim_fd;
	void *shim_mem;
};

/*
 * Endpoint operations
 */
int cxl_fabric_get_endpoints(cxl_fabric_t *fabric,
			      cxl_endpoint_t ***endpoints,
			      size_t *count)
{
	char path[512];
	DIR *dir;
	struct dirent *ent;
	cxl_endpoint_t **ep_array = NULL;
	size_t ep_count = 0;
	size_t ep_capacity = 8;

	if (!fabric || !endpoints || !count)
		return -EINVAL;

	/* Allocate initial array */
	ep_array = calloc(ep_capacity, sizeof(cxl_endpoint_t *));
	if (!ep_array)
		return -ENOMEM;

	/* Scan sysfs for endpoints */
	snprintf(path, sizeof(path), "/sys/class/cxl_fabric/fabric%d",
		 fabric->fabric_id);
	dir = opendir(path);
	if (!dir) {
		free(ep_array);
		return -errno;
	}

	while ((ent = readdir(dir)) != NULL) {
		uint32_t ep_id;
		cxl_endpoint_t *ep;

		if (sscanf(ent->d_name, "endpoint%u", &ep_id) != 1)
			continue;

		/* Resize array if needed */
		if (ep_count >= ep_capacity) {
			ep_capacity *= 2;
			cxl_endpoint_t **new_array = realloc(ep_array,
				ep_capacity * sizeof(cxl_endpoint_t *));
			if (!new_array) {
				closedir(dir);
				free(ep_array);
				return -ENOMEM;
			}
			ep_array = new_array;
		}

		/* Create endpoint */
		ep = calloc(1, sizeof(*ep));
		if (!ep)
			continue;

		ep->fabric = fabric;
		ep->endpoint_id = ep_id;
		ep->ref_count = 1;
		ep_array[ep_count++] = ep;

		fabric->ref_count++;
	}

	closedir(dir);

	*endpoints = ep_array;
	*count = ep_count;

	/* Cache in fabric */
	fabric->endpoints = ep_array;
	fabric->num_endpoints = ep_count;

	return 0;
}

void cxl_fabric_free_endpoints(cxl_endpoint_t **endpoints)
{
	/* Don't actually free - they're cached in fabric */
	/* Real cleanup happens in cxl_fabric_close */
}

cxl_endpoint_t *cxl_fabric_get_endpoint_by_id(cxl_fabric_t *fabric,
					       uint32_t endpoint_id)
{
	cxl_endpoint_t **endpoints = NULL;
	size_t count = 0, i;

	if (!fabric)
		return NULL;

	/* Use cached endpoints if available */
	if (fabric->endpoints) {
		for (i = 0; i < fabric->num_endpoints; i++) {
			if (fabric->endpoints[i]->endpoint_id == endpoint_id) {
				fabric->endpoints[i]->ref_count++;
				return fabric->endpoints[i];
			}
		}
	}

	/* Not in cache, scan */
	if (cxl_fabric_get_endpoints(fabric, &endpoints, &count) < 0)
		return NULL;

	for (i = 0; i < count; i++) {
		if (endpoints[i]->endpoint_id == endpoint_id) {
			endpoints[i]->ref_count++;
			return endpoints[i];
		}
	}

	return NULL;
}

int cxl_endpoint_get_info(cxl_endpoint_t *ep, struct cxl_endpoint_info *info)
{
	char path[512];
	FILE *f;

	if (!ep || !info)
		return -EINVAL;

	memset(info, 0, sizeof(*info));
	info->endpoint_id = ep->endpoint_id;

	/* Read from sysfs */
	snprintf(path, sizeof(path),
		 "/sys/class/cxl_fabric/fabric%d/endpoint%u/total_memory",
		 ep->fabric->fabric_id, ep->endpoint_id);
	f = fopen(path, "r");
	if (f) {
		fscanf(f, "%llu", (unsigned long long *)&info->total_memory);
		fclose(f);
	}

	snprintf(path, sizeof(path),
		 "/sys/class/cxl_fabric/fabric%d/endpoint%u/available_memory",
		 ep->fabric->fabric_id, ep->endpoint_id);
	f = fopen(path, "r");
	if (f) {
		fscanf(f, "%lld", (long long *)&info->available_memory);
		fclose(f);
	}

	snprintf(path, sizeof(path),
		 "/sys/class/cxl_fabric/fabric%d/endpoint%u/numa_node",
		 ep->fabric->fabric_id, ep->endpoint_id);
	f = fopen(path, "r");
	if (f) {
		fscanf(f, "%d", &info->numa_node);
		fclose(f);
	}

	snprintf(path, sizeof(path),
		 "/sys/class/cxl_fabric/fabric%d/endpoint%u/supports_atomic",
		 ep->fabric->fabric_id, ep->endpoint_id);
	f = fopen(path, "r");
	if (f) {
		int val;
		if (fscanf(f, "%d", &val) == 1)
			info->supports_atomic = (val != 0);
		fclose(f);
	}

	snprintf(path, sizeof(path),
		 "/sys/class/cxl_fabric/fabric%d/endpoint%u/supports_coherent",
		 ep->fabric->fabric_id, ep->endpoint_id);
	f = fopen(path, "r");
	if (f) {
		int val;
		if (fscanf(f, "%d", &val) == 1)
			info->supports_coherent = (val != 0);
		fclose(f);
	}

	snprintf(info->name, sizeof(info->name), "endpoint%u", ep->endpoint_id);

	/* Cache info */
	memcpy(&ep->info, info, sizeof(*info));
	ep->info_valid = true;

	return 0;
}

/*
 * Memory region operations
 */
cxl_mem_t *cxl_mem_open(cxl_endpoint_t *ep, size_t size, int access)
{
	struct cxl_mem *mem;
	char shim_path[256];
	int fd;

	if (!ep || size == 0)
		return NULL;

	mem = calloc(1, sizeof(*mem));
	if (!mem)
		return NULL;

	mem->endpoint = ep;
	mem->size = size;
	mem->access = access;
	mem->ref_count = 1;

	/* SHIM: Open simulated memory via tmpfs */
	snprintf(shim_path, sizeof(shim_path),
		 "/dev/shm/cxl_fabric%d_ep%u_%p",
		 ep->fabric->fabric_id, ep->endpoint_id, (void *)mem);

	fd = open(shim_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		free(mem);
		return NULL;
	}

	/* Resize file */
	if (ftruncate(fd, size) < 0) {
		close(fd);
		unlink(shim_path);
		free(mem);
		return NULL;
	}

	mem->shim_fd = fd;

	/* Generate pseudo-handle */
	mem->handle = ((uint64_t)ep->endpoint_id << 32) | (uint64_t)(uintptr_t)mem;

	ep->ref_count++;

	return mem;
}

void cxl_mem_close(cxl_mem_t *mem)
{
	if (!mem)
		return;

	if (--mem->ref_count == 0) {
		if (mem->mapped_addr && mem->mapped_size > 0)
			munmap(mem->mapped_addr, mem->mapped_size);

		if (mem->shim_fd >= 0) {
			char path[256];
			close(mem->shim_fd);
			snprintf(path, sizeof(path),
				 "/dev/shm/cxl_fabric%d_ep%u_%p",
				 mem->endpoint->fabric->fabric_id,
				 mem->endpoint->endpoint_id,
				 (void *)mem);
			unlink(path);
		}

		if (mem->endpoint)
			mem->endpoint->ref_count--;

		free(mem);
	}
}

int cxl_mem_get_info(cxl_mem_t *mem, struct cxl_mem_info *info)
{
	if (!mem || !info)
		return -EINVAL;

	memset(info, 0, sizeof(*info));
	info->handle = mem->handle;
	info->remote_phys_addr = 0x1000000000ULL + mem->handle;
	info->size = mem->size;
	info->access_flags = mem->access;
	info->coherent = (mem->access & CXL_MEM_COHERENT) ? true : false;
	info->read_count = mem->read_count;
	info->write_count = mem->write_count;

	return 0;
}

void *cxl_mem_map(cxl_mem_t *mem, off_t offset, size_t length)
{
	void *addr;
	int prot = PROT_NONE;

	if (!mem || offset + length > mem->size)
		return NULL;

	if (mem->access & CXL_MEM_READ)
		prot |= PROT_READ;
	if (mem->access & CXL_MEM_WRITE)
		prot |= PROT_WRITE;

	addr = mmap(NULL, length, prot, MAP_SHARED, mem->shim_fd, offset);
	if (addr == MAP_FAILED)
		return NULL;

	mem->mapped_addr = addr;
	mem->mapped_size = length;

	return addr;
}

int cxl_mem_unmap(cxl_mem_t *mem, void *addr, size_t length)
{
	if (!mem || !addr)
		return -EINVAL;

	return munmap(addr, length);
}

int cxl_mem_put(cxl_mem_t *mem, const void *src, size_t length, off_t offset)
{
	ssize_t ret;

	if (!mem || !src)
		return -EINVAL;

	if (offset + length > mem->size)
		return -EINVAL;

	if (!(mem->access & CXL_MEM_WRITE))
		return -EACCES;

	ret = pwrite(mem->shim_fd, src, length, offset);
	if (ret < 0)
		return -errno;

	mem->write_count++;

	return 0;
}

int cxl_mem_get(cxl_mem_t *mem, void *dest, size_t length, off_t offset)
{
	ssize_t ret;

	if (!mem || !dest)
		return -EINVAL;

	if (offset + length > mem->size)
		return -EINVAL;

	if (!(mem->access & CXL_MEM_READ))
		return -EACCES;

	ret = pread(mem->shim_fd, dest, length, offset);
	if (ret < 0)
		return -errno;

	mem->read_count++;

	return 0;
}

int cxl_mem_copy(cxl_mem_t *dst, cxl_mem_t *src,
		 size_t length, off_t dst_offset, off_t src_offset)
{
	char buffer[4096];
	size_t remaining = length;
	off_t src_off = src_offset;
	off_t dst_off = dst_offset;

	while (remaining > 0) {
		size_t chunk = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
		int ret;

		ret = cxl_mem_get(src, buffer, chunk, src_off);
		if (ret < 0)
			return ret;

		ret = cxl_mem_put(dst, buffer, chunk, dst_off);
		if (ret < 0)
			return ret;

		remaining -= chunk;
		src_off += chunk;
		dst_off += chunk;
	}

	return 0;
}

/*
 * Atomic operations (SHIM implementation using file locking)
 */
int cxl_mem_atomic_add(cxl_mem_t *mem, off_t offset, int64_t value)
{
	int64_t old_val, new_val;
	struct flock lock;

	if (!mem)
		return -EINVAL;

	if (offset & 7)
		return -EINVAL;

	if (!(mem->access & CXL_MEM_ATOMIC))
		return -EACCES;

	/* Lock region */
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = offset;
	lock.l_len = sizeof(int64_t);

	if (fcntl(mem->shim_fd, F_SETLKW, &lock) < 0)
		return -errno;

	/* Read-modify-write */
	if (pread(mem->shim_fd, &old_val, sizeof(old_val), offset) != sizeof(old_val)) {
		lock.l_type = F_UNLCK;
		fcntl(mem->shim_fd, F_SETLK, &lock);
		return -EIO;
	}

	new_val = old_val + value;

	if (pwrite(mem->shim_fd, &new_val, sizeof(new_val), offset) != sizeof(new_val)) {
		lock.l_type = F_UNLCK;
		fcntl(mem->shim_fd, F_SETLK, &lock);
		return -EIO;
	}

	/* Unlock */
	lock.l_type = F_UNLCK;
	fcntl(mem->shim_fd, F_SETLK, &lock);

	return 0;
}

int cxl_mem_atomic_sub(cxl_mem_t *mem, off_t offset, int64_t value)
{
	return cxl_mem_atomic_add(mem, offset, -value);
}

int cxl_mem_compare_swap(cxl_mem_t *mem, off_t offset,
			  uint64_t old_val, uint64_t new_val,
			  uint64_t *actual_old)
{
	uint64_t current;
	struct flock lock;

	if (!mem)
		return -EINVAL;

	if (offset & 7)
		return -EINVAL;

	if (!(mem->access & CXL_MEM_ATOMIC))
		return -EACCES;

	/* Lock region */
	memset(&lock, 0, sizeof(lock));
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = offset;
	lock.l_len = sizeof(uint64_t);

	if (fcntl(mem->shim_fd, F_SETLKW, &lock) < 0)
		return -errno;

	/* Read current value */
	if (pread(mem->shim_fd, &current, sizeof(current), offset) != sizeof(current)) {
		lock.l_type = F_UNLCK;
		fcntl(mem->shim_fd, F_SETLK, &lock);
		return -EIO;
	}

	if (actual_old)
		*actual_old = current;

	/* Compare and swap */
	if (current == old_val) {
		if (pwrite(mem->shim_fd, &new_val, sizeof(new_val), offset) != sizeof(new_val)) {
			lock.l_type = F_UNLCK;
			fcntl(mem->shim_fd, F_SETLK, &lock);
			return -EIO;
		}
	}

	/* Unlock */
	lock.l_type = F_UNLCK;
	fcntl(mem->shim_fd, F_SETLK, &lock);

	return (current == old_val) ? 0 : -EAGAIN;
}

/*
 * Performance monitoring
 */
void cxl_mem_fence(cxl_mem_t *mem)
{
	if (mem && mem->shim_fd >= 0)
		fsync(mem->shim_fd);
}

uint64_t cxl_mem_get_latency_ns(cxl_mem_t *mem)
{
	/* SHIM: Return simulated latency */
	return 350;  /* ~350ns average */
}

uint64_t cxl_mem_get_bandwidth_mbps(cxl_mem_t *mem)
{
	/* SHIM: Return simulated bandwidth */
	return 64000;  /* 64 GB/s */
}
