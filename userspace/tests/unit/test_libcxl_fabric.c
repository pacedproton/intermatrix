/* SPDX-License-Identifier: Apache-2.0 */
/*
 * libcxl Unit Tests - Fabric Operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcxl/cxl.h"
#include "../test_framework.h"

/* Global context for tests */
static cxl_context_t *ctx = NULL;

/* Test: Fabric open/close */
TEST(fabric_open_close) {
    cxl_fabric_t *fabric;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

    fabric = cxl_fabric_open(ctx, 0);
    /* Note: May be NULL if kernel module not loaded - that's OK for unit test */

    if (fabric) {
        cxl_fabric_close(fabric);
    }

    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Invalid fabric ID */
TEST(invalid_fabric_id) {
    cxl_fabric_t *fabric;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

    /* Try to open non-existent fabric */
    fabric = cxl_fabric_open(ctx, 999);
    ASSERT_NULL(fabric, "Should return NULL for invalid fabric ID");

    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Fabric info retrieval (if module loaded) */
TEST(fabric_get_info) {
    cxl_fabric_t *fabric;
    struct cxl_fabric_info info;
    int ret;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

    fabric = cxl_fabric_open(ctx, 0);
    if (!fabric) {
        /* Kernel module not loaded - skip test */
        printf(COLOR_YELLOW "SKIP" COLOR_RESET " (module not loaded)\n");
        tests_passed++;
        cxl_cleanup(ctx);
        return;
    }

    ret = cxl_fabric_get_info(fabric, &info);
    if (ret == 0) {
        ASSERT_EQ(info.fabric_id, 0, "Fabric ID should be 0");
        /* Just verify we can read the values */
        printf(COLOR_YELLOW "[switches=%u, endpoints=%u]" COLOR_RESET " ",
               info.num_switches, info.num_endpoints);
    }

    cxl_fabric_close(fabric);
    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Multiple fabric open */
TEST(multiple_fabric_open) {
    cxl_fabric_t *fabric1, *fabric2;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

    fabric1 = cxl_fabric_open(ctx, 0);
    fabric2 = cxl_fabric_open(ctx, 0);

    /* Both should succeed (or both fail if module not loaded) */
    if (fabric1 && fabric2) {
        cxl_fabric_close(fabric2);
        cxl_fabric_close(fabric1);
    }

    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: NULL parameter handling */
TEST(fabric_null_params) {
    cxl_fabric_t *fabric;
    struct cxl_fabric_info info;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context initialization failed");

    /* NULL context */
    fabric = cxl_fabric_open(NULL, 0);
    ASSERT_NULL(fabric, "Should return NULL for NULL context");

    /* NULL fabric */
    int ret = cxl_fabric_get_info(NULL, &info);
    ASSERT_NEQ(ret, 0, "Should fail with NULL fabric");

    cxl_cleanup(ctx);
    TEST_PASS();
}

TEST_MAIN_BEGIN()
    TEST_SUITE_BEGIN("libcxl Fabric Tests")
    RUN_TEST(fabric_open_close);
    RUN_TEST(invalid_fabric_id);
    RUN_TEST(fabric_get_info);
    RUN_TEST(multiple_fabric_open);
    RUN_TEST(fabric_null_params);
    TEST_SUITE_END()
TEST_MAIN_END()
