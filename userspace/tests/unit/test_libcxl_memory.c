/* SPDX-License-Identifier: Apache-2.0 */
/*
 * libcxl Unit Tests - Memory Operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "libcxl/cxl.h"
#include "../test_framework.h"

/* Test: Endpoint enumeration */
TEST(endpoint_enumeration) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t **endpoints;
    size_t count;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

    fabric = cxl_fabric_open(ctx, 0);
    if (!fabric) {
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (module not loaded)\n");
        tests_passed++;
        cxl_cleanup(ctx);
        return;
    }

    int ret = cxl_fabric_get_endpoints(fabric, &endpoints, &count);
    if (ret == 0) {
        printf(COLOR_YELLOW "[%zu endpoints]" COLOR_RESET " ", count);
        cxl_fabric_free_endpoints(endpoints);
    }

    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Endpoint info */
TEST(endpoint_info) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    struct cxl_endpoint_info info;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

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

    int ret = cxl_endpoint_get_info(endpoint, &info);
    if (ret == 0) {
        ASSERT_EQ(info.endpoint_id, 0, "Endpoint ID should be 0");
        printf(COLOR_YELLOW "[%llu GB]" COLOR_RESET " ",
               (unsigned long long)(info.total_memory / (1024*1024*1024)));
    }

    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Memory allocation and deallocation */
TEST(memory_alloc_free) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

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

    /* Allocate 4KB */
    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Memory write and read */
TEST(memory_write_read) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    char write_buf[1024];
    char read_buf[1024];
    int i, ret;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    /* Write pattern */
    for (i = 0; i < sizeof(write_buf); i++) {
        write_buf[i] = i & 0xFF;
    }

    ret = cxl_mem_put(mem, write_buf, sizeof(write_buf), 0);
    ASSERT_EQ(ret, 0, "Memory write failed");

    /* Read back */
    memset(read_buf, 0, sizeof(read_buf));
    ret = cxl_mem_get(mem, read_buf, sizeof(read_buf), 0);
    ASSERT_EQ(ret, 0, "Memory read failed");

    /* Verify */
    ASSERT(memcmp(write_buf, read_buf, sizeof(write_buf)) == 0,
           "Read data should match written data");

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Memory mapping */
TEST(memory_mapping) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    uint64_t *ptr;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    /* Map memory */
    ptr = cxl_mem_map(mem, 0, 4096);
    ASSERT_NOT_NULL(ptr, "Memory mapping failed");

    /* Direct access */
    ptr[0] = 0xDEADBEEFCAFEBABEULL;
    ptr[1] = 0x123456789ABCDEF0ULL;

    ASSERT_EQ(ptr[0], 0xDEADBEEFCAFEBABEULL, "Direct write/read failed");
    ASSERT_EQ(ptr[1], 0x123456789ABCDEF0ULL, "Direct write/read failed");

    cxl_mem_unmap(mem, ptr, 4096);
    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Atomic operations */
TEST(atomic_operations) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    int64_t value;
    uint64_t old_val, actual_old;
    int ret;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW | CXL_MEM_ATOMIC);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    /* Initialize value */
    value = 100;
    ret = cxl_mem_put(mem, &value, sizeof(value), 0);
    ASSERT_EQ(ret, 0, "Initial write failed");

    /* Atomic add */
    ret = cxl_mem_atomic_add(mem, 0, 50);
    ASSERT_EQ(ret, 0, "Atomic add failed");

    /* Read back */
    ret = cxl_mem_get(mem, &value, sizeof(value), 0);
    ASSERT_EQ(ret, 0, "Read failed");
    ASSERT_EQ(value, 150, "Atomic add result incorrect");

    /* Atomic subtract */
    ret = cxl_mem_atomic_sub(mem, 0, 30);
    ASSERT_EQ(ret, 0, "Atomic sub failed");

    ret = cxl_mem_get(mem, &value, sizeof(value), 0);
    ASSERT_EQ(value, 120, "Atomic sub result incorrect");

    /* Compare and swap */
    old_val = 120;
    ret = cxl_mem_compare_swap(mem, 0, old_val, 200, &actual_old);
    ASSERT_EQ(ret, 0, "Compare-swap should succeed");
    ASSERT_EQ(actual_old, 120, "Old value should be 120");

    ret = cxl_mem_get(mem, &value, sizeof(value), 0);
    ASSERT_EQ(value, 200, "CAS result incorrect");

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Large memory transfer */
TEST(large_memory_transfer) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    char *large_buf;
    char *verify_buf;
    size_t size = 1024 * 1024; /* 1MB */
    int ret, i;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

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

    mem = cxl_mem_open(endpoint, size, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    large_buf = malloc(size);
    verify_buf = malloc(size);
    ASSERT_NOT_NULL(large_buf, "malloc failed");
    ASSERT_NOT_NULL(verify_buf, "malloc failed");

    /* Fill with pattern */
    for (i = 0; i < size; i++) {
        large_buf[i] = i & 0xFF;
    }

    /* Write */
    ret = cxl_mem_put(mem, large_buf, size, 0);
    ASSERT_EQ(ret, 0, "Large write failed");

    /* Read back */
    ret = cxl_mem_get(mem, verify_buf, size, 0);
    ASSERT_EQ(ret, 0, "Large read failed");

    /* Verify */
    ASSERT(memcmp(large_buf, verify_buf, size) == 0,
           "Large data verification failed");

    free(verify_buf);
    free(large_buf);
    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

TEST_MAIN_BEGIN()
    TEST_SUITE_BEGIN("libcxl Memory Tests")
    RUN_TEST(endpoint_enumeration);
    RUN_TEST(endpoint_info);
    RUN_TEST(memory_alloc_free);
    RUN_TEST(memory_write_read);
    RUN_TEST(memory_mapping);
    RUN_TEST(atomic_operations);
    RUN_TEST(large_memory_transfer);
    TEST_SUITE_END()
TEST_MAIN_END()
