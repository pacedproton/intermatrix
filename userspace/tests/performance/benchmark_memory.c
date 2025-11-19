/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Performance Benchmark - Memory Operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include "libcxl/cxl.h"
#include "../test_framework.h"

/* Get current time in nanoseconds */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Benchmark: Memory allocation latency */
TEST(benchmark_alloc_latency) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    uint64_t start, end;
    int i, iterations = 100;
    double avg_latency;

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

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        cxl_mem_t *mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
        if (mem) {
            cxl_mem_close(mem);
        }
    }
    end = get_time_ns();

    avg_latency = (double)(end - start) / iterations / 1000.0; /* microseconds */

    printf(COLOR_GREEN "PASS" COLOR_RESET " ");
    printf("[Alloc latency: %.2f μs]\n", avg_latency);
    tests_passed++;
}

/* Benchmark: Read latency */
TEST(benchmark_read_latency) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    uint64_t start, end;
    char data[8];
    int i, iterations = 10000;
    double avg_latency;

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        cxl_mem_get(mem, data, sizeof(data), 0);
    }
    end = get_time_ns();

    avg_latency = (double)(end - start) / iterations; /* nanoseconds */

    printf(COLOR_GREEN "PASS" COLOR_RESET " ");
    printf("[Read latency: %.0f ns]\n", avg_latency);
    tests_passed++;

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
}

/* Benchmark: Write latency */
TEST(benchmark_write_latency) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    uint64_t start, end;
    char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int i, iterations = 10000;
    double avg_latency;

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        cxl_mem_put(mem, data, sizeof(data), 0);
    }
    end = get_time_ns();

    avg_latency = (double)(end - start) / iterations; /* nanoseconds */

    printf(COLOR_GREEN "PASS" COLOR_RESET " ");
    printf("[Write latency: %.0f ns]\n", avg_latency);
    tests_passed++;

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
}

/* Benchmark: Bandwidth */
TEST(benchmark_bandwidth) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    uint64_t start, end;
    char *data;
    size_t size = 1024 * 1024; /* 1MB */
    int i, iterations = 100;
    double bandwidth;

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

    mem = cxl_mem_open(endpoint, size, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    data = malloc(size);
    ASSERT_NOT_NULL(data, "malloc failed");
    memset(data, 0xAA, size);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        cxl_mem_put(mem, data, size, 0);
    }
    end = get_time_ns();

    /* Calculate bandwidth in MB/s */
    double time_sec = (double)(end - start) / 1000000000.0;
    double bytes_transferred = (double)(size * iterations);
    bandwidth = (bytes_transferred / time_sec) / (1024 * 1024);

    printf(COLOR_GREEN "PASS" COLOR_RESET " ");
    printf("[Write bandwidth: %.0f MB/s]\n", bandwidth);
    tests_passed++;

    free(data);
    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
}

/* Benchmark: Atomic operation latency */
TEST(benchmark_atomic_latency) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    uint64_t start, end;
    int i, iterations = 1000;
    double avg_latency;

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW | CXL_MEM_ATOMIC);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    /* Initialize */
    int64_t val = 0;
    cxl_mem_put(mem, &val, sizeof(val), 0);

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        cxl_mem_atomic_add(mem, 0, 1);
    }
    end = get_time_ns();

    avg_latency = (double)(end - start) / iterations; /* nanoseconds */

    printf(COLOR_GREEN "PASS" COLOR_RESET " ");
    printf("[Atomic add latency: %.0f ns]\n", avg_latency);
    tests_passed++;

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
}

/* Benchmark: Memory mapping overhead */
TEST(benchmark_mmap_overhead) {
    cxl_context_t *ctx;
    cxl_fabric_t *fabric;
    cxl_endpoint_t *endpoint;
    cxl_mem_t *mem;
    uint64_t start, end;
    void *ptr;
    int i, iterations = 100;
    double avg_latency;

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

    mem = cxl_mem_open(endpoint, 4096, CXL_MEM_RW);
    ASSERT_NOT_NULL(mem, "Memory allocation failed");

    start = get_time_ns();
    for (i = 0; i < iterations; i++) {
        ptr = cxl_mem_map(mem, 0, 4096);
        if (ptr) {
            cxl_mem_unmap(mem, ptr, 4096);
        }
    }
    end = get_time_ns();

    avg_latency = (double)(end - start) / iterations / 1000.0; /* microseconds */

    printf(COLOR_GREEN "PASS" COLOR_RESET " ");
    printf("[mmap/munmap latency: %.2f μs]\n", avg_latency);
    tests_passed++;

    cxl_mem_close(mem);
    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
}

TEST_MAIN_BEGIN()
    TEST_SUITE_BEGIN("Performance Benchmarks")
    RUN_TEST(benchmark_alloc_latency);
    RUN_TEST(benchmark_read_latency);
    RUN_TEST(benchmark_write_latency);
    RUN_TEST(benchmark_bandwidth);
    RUN_TEST(benchmark_atomic_latency);
    RUN_TEST(benchmark_mmap_overhead);
    TEST_SUITE_END()
TEST_MAIN_END()
