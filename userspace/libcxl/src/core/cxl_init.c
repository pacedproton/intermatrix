/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * libcxl - Core Library Initialization
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "libcxl/cxl.h"
#include "../internal.h"

#define CXL_SYSFS_PATH "/sys/class/cxl_fabric"

/*
 * Context structure (opaque)
 */
struct cxl_context {
	int last_error;
	unsigned int ref_count;
	bool initialized;
};

/*
 * Fabric structure (opaque)
 */
struct cxl_fabric {
	struct cxl_context *ctx;
	int fabric_id;
	int fd;  /* Device file descriptor */
	char dev_path[256];

	/* Cached info */
	struct cxl_fabric_info info;
	bool info_valid;

	/* Endpoint list */
	cxl_endpoint_t **endpoints;
	size_t num_endpoints;

	unsigned int ref_count;
};

/*
 * Endpoint structure (opaque)
 */
struct cxl_endpoint {
	cxl_fabric_t *fabric;
	uint32_t endpoint_id;

	/* Cached info */
	struct cxl_endpoint_info info;
	bool info_valid;

	unsigned int ref_count;
};

/*
 * Memory region structure (opaque)
 */
struct cxl_mem {
	cxl_endpoint_t *endpoint;
	size_t size;
	int access;

	/* Kernel region handle */
	uint64_t handle;

	/* Mapping */
	void *mapped_addr;
	size_t mapped_size;

	/* Statistics */
	uint64_t read_count;
	uint64_t write_count;

	unsigned int ref_count;
};

/*
 * Version information
 */
const char *cxl_get_version(void)
{
	static char version[64];
	snprintf(version, sizeof(version), "%d.%d.%d",
		 LIBCXL_VERSION_MAJOR,
		 LIBCXL_VERSION_MINOR,
		 LIBCXL_VERSION_PATCH);
	return version;
}

int cxl_get_version_number(int *major, int *minor, int *patch)
{
	if (major)
		*major = LIBCXL_VERSION_MAJOR;
	if (minor)
		*minor = LIBCXL_VERSION_MINOR;
	if (patch)
		*patch = LIBCXL_VERSION_PATCH;
	return 0;
}

/*
 * Library initialization
 */
cxl_context_t *cxl_init(void)
{
	cxl_context_t *ctx;
	struct stat st;

	/* Check if CXL fabric subsystem is available */
	if (stat(CXL_SYSFS_PATH, &st) != 0) {
		fprintf(stderr, "libcxl: CXL fabric subsystem not available\n");
		fprintf(stderr, "libcxl: Is the cxl_interconnect kernel module loaded?\n");
		return NULL;
	}

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->ref_count = 1;
	ctx->initialized = true;
	ctx->last_error = 0;

	return ctx;
}

void cxl_cleanup(cxl_context_t *ctx)
{
	if (!ctx)
		return;

	if (--ctx->ref_count == 0) {
		ctx->initialized = false;
		free(ctx);
	}
}

/*
 * Error handling
 */
const char *cxl_strerror(int error)
{
	return strerror(error);
}

int cxl_get_last_error(cxl_context_t *ctx)
{
	return ctx ? ctx->last_error : EINVAL;
}

static void cxl_set_error(cxl_context_t *ctx, int error)
{
	if (ctx)
		ctx->last_error = error;
}

/*
 * Fabric management
 */
cxl_fabric_t *cxl_fabric_open(cxl_context_t *ctx, int fabric_id)
{
	cxl_fabric_t *fabric;
	char path[256];
	struct stat st;
	int fd;

	if (!ctx || !ctx->initialized) {
		cxl_set_error(ctx, EINVAL);
		return NULL;
	}

	/* Check if fabric exists */
	snprintf(path, sizeof(path), "%s/fabric%d", CXL_SYSFS_PATH, fabric_id);
	if (stat(path, &st) != 0) {
		cxl_set_error(ctx, ENOENT);
		return NULL;
	}

	/* Open device file */
	snprintf(path, sizeof(path), "/dev/cxl_fabric%d", fabric_id);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		/* Fallback: fabric exists in sysfs but no dev node yet */
		/* This is OK for now (SHIM mode) */
		fd = -1;
	}

	fabric = calloc(1, sizeof(*fabric));
	if (!fabric) {
		if (fd >= 0)
			close(fd);
		cxl_set_error(ctx, ENOMEM);
		return NULL;
	}

	fabric->ctx = ctx;
	fabric->fabric_id = fabric_id;
	fabric->fd = fd;
	fabric->ref_count = 1;
	snprintf(fabric->dev_path, sizeof(fabric->dev_path), "%s", path);

	ctx->ref_count++;

	return fabric;
}

void cxl_fabric_close(cxl_fabric_t *fabric)
{
	if (!fabric)
		return;

	if (--fabric->ref_count == 0) {
		/* Free endpoint list */
		if (fabric->endpoints) {
			size_t i;
			for (i = 0; i < fabric->num_endpoints; i++) {
				if (fabric->endpoints[i])
					fabric->endpoints[i]->ref_count--;
			}
			free(fabric->endpoints);
		}

		if (fabric->fd >= 0)
			close(fabric->fd);

		if (fabric->ctx)
			cxl_cleanup(fabric->ctx);

		free(fabric);
	}
}

/*
 * Fabric information
 */
int cxl_fabric_get_info(cxl_fabric_t *fabric, struct cxl_fabric_info *info)
{
	char path[512];
	FILE *f;
	uint32_t val;

	if (!fabric || !info)
		return -EINVAL;

	/* Read from sysfs */
	info->fabric_id = fabric->fabric_id;

	/* Read num_switches */
	snprintf(path, sizeof(path), "%s/fabric%d/num_switches",
		 CXL_SYSFS_PATH, fabric->fabric_id);
	f = fopen(path, "r");
	if (f) {
		if (fscanf(f, "%u", &val) == 1)
			info->num_switches = val;
		fclose(f);
	}

	/* Read num_endpoints */
	snprintf(path, sizeof(path), "%s/fabric%d/num_endpoints",
		 CXL_SYSFS_PATH, fabric->fabric_id);
	f = fopen(path, "r");
	if (f) {
		if (fscanf(f, "%u", &val) == 1)
			info->num_endpoints = val;
		fclose(f);
	}

	/* Read generation */
	snprintf(path, sizeof(path), "%s/fabric%d/generation",
		 CXL_SYSFS_PATH, fabric->fabric_id);
	f = fopen(path, "r");
	if (f) {
		if (fscanf(f, "%u", &val) == 1)
			info->generation = val;
		fclose(f);
	}

	/* Cache info */
	memcpy(&fabric->info, info, sizeof(*info));
	fabric->info_valid = true;

	return 0;
}

int cxl_fabric_scan(cxl_fabric_t *fabric)
{
	/* In real implementation, this would trigger kernel fabric scan */
	/* For now, just invalidate cached info */
	if (!fabric)
		return -EINVAL;

	fabric->info_valid = false;

	return 0;
}
