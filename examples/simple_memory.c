/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Simple CXL Memory Example
 *
 * Demonstrates basic memory allocation and access via CXL fabric
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/cxl.h"

int main(void)
{
	cxl_context_t *ctx;
	cxl_fabric_t *fabric;
	cxl_endpoint_t *endpoint;
	cxl_mem_t *mem;
	struct cxl_fabric_info fabric_info;
	struct cxl_endpoint_info ep_info;
	uint64_t *ptr;
	int i;

	printf("CXL Interconnect - Simple Memory Example\n");
	printf("=========================================\n\n");

	/* Initialize CXL library */
	ctx = cxl_init();
	if (!ctx) {
		fprintf(stderr, "Error: Failed to initialize libcxl\n");
		fprintf(stderr, "Is the cxl_interconnect kernel module loaded?\n");
		return 1;
	}

	printf("✓ Initialized libcxl v%s\n", cxl_get_version());

	/* Open fabric 0 */
	fabric = cxl_fabric_open(ctx, 0);
	if (!fabric) {
		fprintf(stderr, "Error: Failed to open fabric 0\n");
		cxl_cleanup(ctx);
		return 1;
	}

	/* Get fabric information */
	if (cxl_fabric_get_info(fabric, &fabric_info) == 0) {
		printf("✓ Opened fabric %u (%u switches, %u endpoints)\n",
		       fabric_info.fabric_id,
		       fabric_info.num_switches,
		       fabric_info.num_endpoints);
	}

	/* Get first endpoint */
	endpoint = cxl_fabric_get_endpoint_by_id(fabric, 0);
	if (!endpoint) {
		fprintf(stderr, "Error: No endpoints found\n");
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	/* Get endpoint information */
	if (cxl_endpoint_get_info(endpoint, &ep_info) == 0) {
		printf("✓ Using endpoint %u:\n", ep_info.endpoint_id);
		printf("    Total memory:     %llu GB\n",
		       ep_info.total_memory / (1024*1024*1024));
		printf("    Available memory: %llu GB\n",
		       ep_info.available_memory / (1024*1024*1024));
		printf("    NUMA node:        %d\n", ep_info.numa_node);
		printf("    Atomic ops:       %s\n",
		       ep_info.supports_atomic ? "Yes" : "No");
		printf("    Cache coherent:   %s\n",
		       ep_info.supports_coherent ? "Yes" : "No");
	}

	/* Allocate 4KB of remote memory */
	printf("\n");
	mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW | CXL_MEM_COHERENT);
	if (!mem) {
		fprintf(stderr, "Error: Failed to allocate remote memory\n");
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	printf("✓ Allocated 4KB remote memory region\n");

	/* Map memory into local address space */
	ptr = cxl_mem_map(mem, 0, 4096);
	if (!ptr) {
		fprintf(stderr, "Error: Failed to map memory\n");
		cxl_mem_close(mem);
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	printf("✓ Mapped memory to local address space\n");

	/* Write pattern to remote memory */
	printf("\nWriting pattern to remote memory...\n");
	for (i = 0; i < 512; i++) {  /* 512 * 8 = 4096 bytes */
		ptr[i] = 0x1000 + i;
	}

	/* Read back and verify */
	printf("Reading back and verifying...\n");
	for (i = 0; i < 512; i++) {
		if (ptr[i] != (uint64_t)(0x1000 + i)) {
			fprintf(stderr, "Error: Data mismatch at index %d\n", i);
			break;
		}
	}

	if (i == 512) {
		printf("✓ Data verification successful!\n");
	}

	/* Demonstrate direct memory access */
	printf("\nDirect memory access example:\n");
	ptr[0] = 0xDEADBEEFCAFEBABEULL;
	ptr[1] = 0x123456789ABCDEF0ULL;

	printf("  Wrote: 0x%016llx at offset 0\n",
	       (unsigned long long)0xDEADBEEFCAFEBABEULL);
	printf("  Wrote: 0x%016llx at offset 8\n",
	       (unsigned long long)0x123456789ABCDEF0ULL);

	printf("  Read:  0x%016llx from offset 0\n", (unsigned long long)ptr[0]);
	printf("  Read:  0x%016llx from offset 8\n", (unsigned long long)ptr[1]);

	/* Performance info */
	printf("\nPerformance characteristics:\n");
	printf("  Latency:   ~%llu ns\n",
	       (unsigned long long)cxl_mem_get_latency_ns(mem));
	printf("  Bandwidth: ~%llu MB/s\n",
	       (unsigned long long)cxl_mem_get_bandwidth_mbps(mem));

	/* Cleanup */
	cxl_mem_unmap(mem, ptr, 4096);
	cxl_mem_close(mem);
	cxl_fabric_close(fabric);
	cxl_cleanup(ctx);

	printf("\n✓ Cleanup complete\n");
	printf("\nExample completed successfully!\n");

	return 0;
}
