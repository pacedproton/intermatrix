/* SPDX-License-Identifier: Apache-2.0 */
/*
 * cxl-cli - CXL Fabric Management Tool
 *
 * Copyright (C) 2025 InterMatrix Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "libcxl/cxl.h"

static void print_version(void)
{
	printf("cxl-cli version %s\n", cxl_get_version());
	printf("libcxl version %s\n", cxl_get_version());
}

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [options]\n\n", prog);
	printf("Commands:\n");
	printf("  fabric list                     List all CXL fabrics\n");
	printf("  fabric show <fabric_id>         Show fabric details\n");
	printf("  endpoint list <fabric_id>       List endpoints in fabric\n");
	printf("  endpoint info <fabric_id> <ep_id>  Show endpoint information\n");
	printf("  mem alloc <fabric_id> <ep_id> <size>  Allocate memory region\n");
	printf("  mem test <fabric_id> <ep_id>    Test memory operations\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help                      Show this help\n");
	printf("  -v, --version                   Show version\n");
}

static int cmd_fabric_list(int argc, char **argv)
{
	cxl_context_t *ctx;
	int i;

	ctx = cxl_init();
	if (!ctx) {
		fprintf(stderr, "Failed to initialize libcxl\n");
		return 1;
	}

	printf("%-8s %-12s %-12s %s\n",
	       "FABRIC", "SWITCHES", "ENDPOINTS", "GENERATION");
	printf("%-8s %-12s %-12s %s\n",
	       "------", "--------", "---------", "----------");

	/* Try to open fabric 0-7 */
	for (i = 0; i < 8; i++) {
		cxl_fabric_t *fabric;
		struct cxl_fabric_info info;

		fabric = cxl_fabric_open(ctx, i);
		if (!fabric)
			continue;

		if (cxl_fabric_get_info(fabric, &info) == 0) {
			printf("%-8u %-12u %-12u %u\n",
			       info.fabric_id,
			       info.num_switches,
			       info.num_endpoints,
			       info.generation);
		}

		cxl_fabric_close(fabric);
	}

	cxl_cleanup(ctx);
	return 0;
}

static int cmd_fabric_show(int argc, char **argv)
{
	cxl_context_t *ctx;
	cxl_fabric_t *fabric;
	struct cxl_fabric_info info;
	int fabric_id;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s fabric show <fabric_id>\n", argv[0]);
		return 1;
	}

	fabric_id = atoi(argv[2]);

	ctx = cxl_init();
	if (!ctx) {
		fprintf(stderr, "Failed to initialize libcxl\n");
		return 1;
	}

	fabric = cxl_fabric_open(ctx, fabric_id);
	if (!fabric) {
		fprintf(stderr, "Failed to open fabric %d\n", fabric_id);
		cxl_cleanup(ctx);
		return 1;
	}

	if (cxl_fabric_get_info(fabric, &info) < 0) {
		fprintf(stderr, "Failed to get fabric info\n");
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	printf("Fabric %u:\n", info.fabric_id);
	printf("  Switches:   %u\n", info.num_switches);
	printf("  Endpoints:  %u\n", info.num_endpoints);
	printf("  Generation: %u\n", info.generation);

	cxl_fabric_close(fabric);
	cxl_cleanup(ctx);
	return 0;
}

static int cmd_endpoint_list(int argc, char **argv)
{
	cxl_context_t *ctx;
	cxl_fabric_t *fabric;
	cxl_endpoint_t **endpoints;
	size_t count, i;
	int fabric_id;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s endpoint list <fabric_id>\n", argv[0]);
		return 1;
	}

	fabric_id = atoi(argv[2]);

	ctx = cxl_init();
	if (!ctx) {
		fprintf(stderr, "Failed to initialize libcxl\n");
		return 1;
	}

	fabric = cxl_fabric_open(ctx, fabric_id);
	if (!fabric) {
		fprintf(stderr, "Failed to open fabric %d\n", fabric_id);
		cxl_cleanup(ctx);
		return 1;
	}

	if (cxl_fabric_get_endpoints(fabric, &endpoints, &count) < 0) {
		fprintf(stderr, "Failed to get endpoints\n");
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	printf("%-12s %-16s %-16s %-8s %s\n",
	       "ENDPOINT", "TOTAL_MEMORY", "AVAIL_MEMORY", "NUMA", "FLAGS");
	printf("%-12s %-16s %-16s %-8s %s\n",
	       "--------", "------------", "------------", "----", "-----");

	for (i = 0; i < count; i++) {
		struct cxl_endpoint_info info;

		if (cxl_endpoint_get_info(endpoints[i], &info) < 0)
			continue;

		printf("%-12u %-16llu %-16llu %-8d %s%s\n",
		       info.endpoint_id,
		       (unsigned long long)info.total_memory,
		       (unsigned long long)info.available_memory,
		       info.numa_node,
		       info.supports_atomic ? "A" : "-",
		       info.supports_coherent ? "C" : "-");
	}

	cxl_fabric_free_endpoints(endpoints);
	cxl_fabric_close(fabric);
	cxl_cleanup(ctx);
	return 0;
}

static int cmd_mem_test(int argc, char **argv)
{
	cxl_context_t *ctx;
	cxl_fabric_t *fabric;
	cxl_endpoint_t *endpoint;
	cxl_mem_t *mem;
	int fabric_id, endpoint_id;
	char write_buf[4096];
	char read_buf[4096];
	int i;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s mem test <fabric_id> <endpoint_id>\n", argv[0]);
		return 1;
	}

	fabric_id = atoi(argv[2]);
	endpoint_id = atoi(argv[3]);

	ctx = cxl_init();
	if (!ctx) {
		fprintf(stderr, "Failed to initialize libcxl\n");
		return 1;
	}

	fabric = cxl_fabric_open(ctx, fabric_id);
	if (!fabric) {
		fprintf(stderr, "Failed to open fabric %d\n", fabric_id);
		cxl_cleanup(ctx);
		return 1;
	}

	endpoint = cxl_fabric_get_endpoint_by_id(fabric, endpoint_id);
	if (!endpoint) {
		fprintf(stderr, "Failed to get endpoint %d\n", endpoint_id);
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	printf("Testing memory operations on endpoint %u...\n", endpoint_id);

	/* Allocate 1MB */
	mem = cxl_mem_open(endpoint, 1024 * 1024, CXL_MEM_RW | CXL_MEM_ATOMIC);
	if (!mem) {
		fprintf(stderr, "Failed to allocate memory\n");
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	printf("  Allocated 1MB region\n");

	/* Test 1: Write and read */
	printf("  Test 1: Write and read...\n");
	for (i = 0; i < sizeof(write_buf); i++)
		write_buf[i] = i & 0xff;

	if (cxl_mem_put(mem, write_buf, sizeof(write_buf), 0) < 0) {
		fprintf(stderr, "Failed to write memory\n");
		cxl_mem_close(mem);
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	memset(read_buf, 0, sizeof(read_buf));
	if (cxl_mem_get(mem, read_buf, sizeof(read_buf), 0) < 0) {
		fprintf(stderr, "Failed to read memory\n");
		cxl_mem_close(mem);
		cxl_fabric_close(fabric);
		cxl_cleanup(ctx);
		return 1;
	}

	if (memcmp(write_buf, read_buf, sizeof(write_buf)) == 0) {
		printf("    PASS: Data verified\n");
	} else {
		printf("    FAIL: Data mismatch\n");
	}

	/* Test 2: mmap */
	printf("  Test 2: Memory mapping...\n");
	{
		uint64_t *ptr = cxl_mem_map(mem, 0, 4096);
		if (ptr) {
			ptr[0] = 0xDEADBEEFCAFEBABE;
			ptr[1] = 0x123456789ABCDEF0;
			printf("    PASS: Mapped and wrote via pointer\n");

			uint64_t val0 = ptr[0];
			uint64_t val1 = ptr[1];
			if (val0 == 0xDEADBEEFCAFEBABE && val1 == 0x123456789ABCDEF0) {
				printf("    PASS: Read back correct values\n");
			}

			cxl_mem_unmap(mem, ptr, 4096);
		}
	}

	/* Test 3: Atomic operations */
	printf("  Test 3: Atomic operations...\n");
	{
		int64_t initial = 100;
		int64_t result;

		cxl_mem_put(mem, &initial, sizeof(initial), 0);

		cxl_mem_atomic_add(mem, 0, 50);

		cxl_mem_get(mem, &result, sizeof(result), 0);

		if (result == 150) {
			printf("    PASS: Atomic add (100 + 50 = %lld)\n",
			       (long long)result);
		} else {
			printf("    FAIL: Atomic add (expected 150, got %lld)\n",
			       (long long)result);
		}
	}

	/* Performance test */
	printf("  Performance: Latency ~%llu ns, Bandwidth ~%llu MB/s\n",
	       (unsigned long long)cxl_mem_get_latency_ns(mem),
	       (unsigned long long)cxl_mem_get_bandwidth_mbps(mem));

	printf("All tests completed!\n");

	cxl_mem_close(mem);
	cxl_fabric_close(fabric);
	cxl_cleanup(ctx);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	/* Handle global options */
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		print_usage(argv[0]);
		return 0;
	}

	if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
		print_version();
		return 0;
	}

	/* Dispatch commands */
	if (strcmp(argv[1], "fabric") == 0 && argc >= 3) {
		if (strcmp(argv[2], "list") == 0)
			return cmd_fabric_list(argc, argv);
		if (strcmp(argv[2], "show") == 0)
			return cmd_fabric_show(argc, argv);
	}

	if (strcmp(argv[1], "endpoint") == 0 && argc >= 3) {
		if (strcmp(argv[2], "list") == 0)
			return cmd_endpoint_list(argc, argv);
	}

	if (strcmp(argv[1], "mem") == 0 && argc >= 3) {
		if (strcmp(argv[2], "test") == 0)
			return cmd_mem_test(argc, argv);
	}

	fprintf(stderr, "Unknown command: %s\n", argv[1]);
	print_usage(argv[0]);
	return 1;
}
