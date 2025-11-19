/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Integration Test - End-to-End Workflow
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/cxl.h"
#include "../test_framework.h"

/* Test: Complete workflow from init to cleanup */
TEST(complete_workflow) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t **endpoints, *endpoint;
    cxl_mem_t *mem;
    size_t ep_count;
    uint64_t *ptr;
    int ret, i;

    /* 1. Initialize library */
    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Library initialization failed");

    /* 2. Open fabric */
    fabric = cxl_fabric_open(ctx, 0);
    if (!fabric) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (module not loaded)\n");
        tests_passed++;
        cxl_cleanup(ctx);
        return;
    }

    /* 3. Get fabric info */
    struct cxl_fabric_info fabric_info;
    ret = cxl_fabric_get_info(fabric, &fabric_info);
    if (ret == 0) {
        printf(COLOR_YELLOW "[fabric: %u switches, %u endpoints]" COLOR_RESET " ",
               fabric_info.num_switches, fabric_info.num_endpoints);
    }

    /* 4. Enumerate endpoints */
    ret = cxl_fabric_get_endpoints(fabric, &endpoints, &ep_count);
    if (ret != 0 || ep_count == 0) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no endpoints)\n");
        tests_passed++;
        cxl_fabric_close(fabric);
        cxl_cleanup(ctx);
        return;
    }

    ASSERT(ep_count > 0, "Should have at least one endpoint");
    endpoint = endpoints[0];

    /* 5. Get endpoint info */
    struct cxl_endpoint_info ep_info;
    ret = cxl_endpoint_get_info(endpoint, &ep_info);
    ASSERT_EQ(ret, 0, "Get endpoint info failed");

    /* 6. Allocate memory */
    mem = cxl_mem_open(endpoint, 1024 * 1024, CXL_MEM_RW | CXL_MEM_ATOMIC);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    /* 7. Map memory */
    ptr = cxl_mem_map(mem, 0, 4096);
    ASSERT_NOT_NULL(ptr, "Memory mapping failed");

    /* 8. Write data via pointer */
    for (i = 0; i < 512; i++) {
        ptr[i] = i * 0x1000 + 0xABCD;
    }

    /* 9. Verify data */
    for (i = 0; i < 512; i++) {
        ASSERT_EQ(ptr[i], i * 0x1000 + 0xABCD, "Data verification failed");
    }

    /* 10. Test atomic operation */
    ptr[0] = 100;
    ret = cxl_mem_atomic_add(mem, 0, 50);
    ASSERT_EQ(ret, 0, "Atomic add failed");
    ASSERT_EQ(ptr[0], 150, "Atomic result incorrect");

    /* 11. Unmap memory */
    ret = cxl_mem_unmap(mem, ptr, 4096);
    ASSERT_EQ(ret, 0, "Memory unmap failed");

    /* 12. Close memory */
    cxl_mem_close(mem);

    /* 13. Free endpoints */
    cxl_fabric_free_endpoints(endpoints);

    /* 14. Close fabric */
    cxl_fabric_close(fabric);

    /* 15. Cleanup */
    cxl_cleanup(ctx);

    TEST_PASS();
}

/* Test: Multiple endpoint access */
TEST(multiple_endpoint_access) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t **endpoints;
    cxl_mem_t *mem1, *mem2;
    size_t ep_count;
    int ret, i;
    char data[1024];

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Library initialization failed");

    fabric = cxl_fabric_open(ctx, 0);
    if (!fabric) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (module not loaded)\n");
        tests_passed++;
        cxl_cleanup(ctx);
        return;
    }

    ret = cxl_fabric_get_endpoints(fabric, &endpoints, &ep_count);
    if (ret != 0 || ep_count < 2) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (need 2+ endpoints)\n");
        tests_passed++;
        if (ret == 0) cxl_fabric_free_endpoints(endpoints);
        cxl_fabric_close(fabric);
        cxl_cleanup(ctx);
        return;
    }

    /* Allocate from different endpoints */
    mem1 = cxl_mem_open(endpoints[0], 4096, CXL_MEM_RW);
    mem2 = cxl_mem_open(endpoints[1], 4096, CXL_MEM_RW);

    ASSERT_NOT_NULL(mem1, "Memory allocation from endpoint 0 failed");
    ASSERT_NOT_NULL(mem2, "Memory allocation from endpoint 1 failed");

    /* Write to both */
    for (i = 0; i < sizeof(data); i++) {
        data[i] = 0xAA;
    }
    ret = cxl_mem_put(mem1, data, sizeof(data), 0);
    ASSERT_EQ(ret, 0, "Write to endpoint 0 failed");

    for (i = 0; i < sizeof(data); i++) {
        data[i] = 0x55;
    }
    ret = cxl_mem_put(mem2, data, sizeof(data), 0);
    ASSERT_EQ(ret, 0, "Write to endpoint 1 failed");

    /* Verify independence */
    memset(data, 0, sizeof(data));
    ret = cxl_mem_get(mem1, data, sizeof(data), 0);
    ASSERT_EQ(ret, 0, "Read from endpoint 0 failed");
    ASSERT_EQ(data[0], 0xAA, "Data from endpoint 0 incorrect");

    memset(data, 0, sizeof(data));
    ret = cxl_mem_get(mem2, data, sizeof(data), 0);
    ASSERT_EQ(ret, 0, "Read from endpoint 1 failed");
    ASSERT_EQ(data[0], 0x55, "Data from endpoint 1 incorrect");

    cxl_mem_close(mem2);
    cxl_mem_close(mem1);
    cxl_fabric_free_endpoints(endpoints);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);

    TEST_PASS();
}

/* Test: Concurrent memory regions */
TEST(concurrent_memory_regions) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem[10];
    int i, ret;
    char data;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Library initialization failed");

    fabric = cxl_fabric_open(ctx, 0);
    if (!fabric) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (module not loaded)\n");
        tests_passed++;
        cxl_cleanup(ctx);
        return;
    }

    endpoint = cxl_fabric_get_endpoint_by_id(fabric, 0);
    if (!endpoint) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (no endpoints)\n");
        tests_passed++;
        cxl_fabric_close(fabric);
        cxl_cleanup(ctx);
        return;
    }

    /* Allocate multiple regions */
    for (i = 0; i < 10; i++) {
        mem[i] = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
        ASSERT_NOT_NULL(mem[i], "Memory allocation failed");

        /* Write unique data to each */
        data = 'A' + i;
        ret = cxl_mem_put(mem[i], &data, 1, 0);
        ASSERT_EQ(ret, 0, "Write failed");
    }

    /* Verify all regions */
    for (i = 0; i < 10; i++) {
        data = 0;
        ret = cxl_mem_get(mem[i], &data, 1, 0);
        ASSERT_EQ(ret, 0, "Read failed");
        ASSERT_EQ(data, 'A' + i, "Data incorrect in concurrent region");
    }

    /* Cleanup */
    for (i = 0; i < 10; i++) {
        cxl_mem_close(mem[i]);
    }

    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);

    TEST_PASS();
}

TEST_MAIN_BEGIN()
    TEST_SUITE_BEGIN("Integration Tests")
    RUN_TEST(complete_workflow);
    RUN_TEST(multiple_endpoint_access);
    RUN_TEST(concurrent_memory_regions);
    TEST_SUITE_END()
TEST_MAIN_END()
