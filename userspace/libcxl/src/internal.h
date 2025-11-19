/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * libcxl - Internal Definitions
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#ifndef _LIBCXL_INTERNAL_H
#define _LIBCXL_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "libcxl/cxl.h"

/* IOCTL definitions (must match kernel) */
#define CXL_IOC_MAGIC 'C'

#define CXL_IOC_ALLOC_REGION    _IOWR(CXL_IOC_MAGIC, 1, struct cxl_ioctl_alloc)
#define CXL_IOC_FREE_REGION     _IOW(CXL_IOC_MAGIC, 2, uint64_t)
#define CXL_IOC_GET_REGION_INFO _IOWR(CXL_IOC_MAGIC, 3, struct cxl_ioctl_region_info)

struct cxl_ioctl_alloc {
	uint32_t endpoint_id;
	uint64_t size;
	uint32_t access_flags;
	uint64_t handle;  /* out */
};

struct cxl_ioctl_region_info {
	uint64_t handle;
	uint64_t remote_phys_addr;  /* out */
	uint64_t size;              /* out */
	uint32_t access_flags;      /* out */
};

/* Internal utility functions */
int cxl_read_sysfs_u32(const char *path, uint32_t *value);
int cxl_read_sysfs_u64(const char *path, uint64_t *value);
int cxl_read_sysfs_string(const char *path, char *buf, size_t size);

#endif /* _LIBCXL_INTERNAL_H */
