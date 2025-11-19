/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * libcxl - CXL Fabric Interconnect Library
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#ifndef _LIBCXL_CXL_H
#define _LIBCXL_CXL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/* Library version */
#define LIBCXL_VERSION_MAJOR 1
#define LIBCXL_VERSION_MINOR 0
#define LIBCXL_VERSION_PATCH 0

/* Memory access modes */
#define CXL_MEM_READ       0x01
#define CXL_MEM_WRITE      0x02
#define CXL_MEM_RW         0x03
#define CXL_MEM_EXEC       0x04
#define CXL_MEM_ATOMIC     0x08
#define CXL_MEM_COHERENT   0x10

/* Opaque types */
typedef struct cxl_context cxl_context_t;
typedef struct cxl_fabric cxl_fabric_t;
typedef struct cxl_endpoint cxl_endpoint_t;
typedef struct cxl_mem cxl_mem_t;

/*
 * Endpoint information
 */
struct cxl_endpoint_info {
	uint32_t endpoint_id;
	uint64_t total_memory;
	uint64_t available_memory;
	int numa_node;
	bool supports_atomic;
	bool supports_coherent;
	char name[64];
};

/*
 * Fabric information
 */
struct cxl_fabric_info {
	uint32_t fabric_id;
	uint32_t num_switches;
	uint32_t num_endpoints;
	uint32_t generation;
};

/*
 * Memory region information
 */
struct cxl_mem_info {
	uint64_t handle;
	uint64_t remote_phys_addr;
	size_t size;
	uint32_t access_flags;
	bool coherent;
	uint64_t read_count;
	uint64_t write_count;
};

/*
 * Library initialization
 */
cxl_context_t *cxl_init(void);
void cxl_cleanup(cxl_context_t *ctx);

const char *cxl_get_version(void);
int cxl_get_version_number(int *major, int *minor, int *patch);

/*
 * Fabric management
 */
cxl_fabric_t *cxl_fabric_open(cxl_context_t *ctx, int fabric_id);
void cxl_fabric_close(cxl_fabric_t *fabric);

int cxl_fabric_get_info(cxl_fabric_t *fabric, struct cxl_fabric_info *info);
int cxl_fabric_scan(cxl_fabric_t *fabric);

/*
 * Endpoint management
 */
int cxl_fabric_get_endpoints(cxl_fabric_t *fabric,
			      cxl_endpoint_t ***endpoints,
			      size_t *count);
void cxl_fabric_free_endpoints(cxl_endpoint_t **endpoints);

cxl_endpoint_t *cxl_fabric_get_endpoint_by_id(cxl_fabric_t *fabric,
					       uint32_t endpoint_id);

int cxl_endpoint_get_info(cxl_endpoint_t *ep, struct cxl_endpoint_info *info);

/*
 * Memory operations
 */
cxl_mem_t *cxl_mem_open(cxl_endpoint_t *ep, size_t size, int access);
void cxl_mem_close(cxl_mem_t *mem);

int cxl_mem_get_info(cxl_mem_t *mem, struct cxl_mem_info *info);

void *cxl_mem_map(cxl_mem_t *mem, off_t offset, size_t length);
int cxl_mem_unmap(cxl_mem_t *mem, void *addr, size_t length);

int cxl_mem_put(cxl_mem_t *mem, const void *src, size_t length, off_t offset);
int cxl_mem_get(cxl_mem_t *mem, void *dest, size_t length, off_t offset);

int cxl_mem_copy(cxl_mem_t *dst, cxl_mem_t *src,
		 size_t length, off_t dst_offset, off_t src_offset);

/*
 * Atomic operations
 */
int cxl_mem_atomic_add(cxl_mem_t *mem, off_t offset, int64_t value);
int cxl_mem_atomic_sub(cxl_mem_t *mem, off_t offset, int64_t value);
int cxl_mem_atomic_and(cxl_mem_t *mem, off_t offset, uint64_t value);
int cxl_mem_atomic_or(cxl_mem_t *mem, off_t offset, uint64_t value);
int cxl_mem_atomic_xor(cxl_mem_t *mem, off_t offset, uint64_t value);

int cxl_mem_compare_swap(cxl_mem_t *mem, off_t offset,
			  uint64_t old_val, uint64_t new_val,
			  uint64_t *actual_old);

/*
 * Performance monitoring
 */
void cxl_mem_fence(cxl_mem_t *mem);
uint64_t cxl_mem_get_latency_ns(cxl_mem_t *mem);
uint64_t cxl_mem_get_bandwidth_mbps(cxl_mem_t *mem);

/*
 * Error handling
 */
const char *cxl_strerror(int error);
int cxl_get_last_error(cxl_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _LIBCXL_CXL_H */
