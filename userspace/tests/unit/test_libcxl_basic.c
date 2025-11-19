/* SPDX-License-Identifier: Apache-2.0 */
/*
 * libcxl Unit Tests - Basic Functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libcxl/cxl.h"
#include "../test_framework.h"

/* Test: Library initialization */
TEST(library_init) {
    cxl_context_t *ctx;

    ctx = cxl_init();
    ASSERT_NOT_NULL(ctx, "Context should not be NULL");

    cxl_cleanup(ctx);
    TEST_PASS();
}

/* Test: Version information */
TEST(version_info) {
    const char *version;
    int major, minor, patch;

    version = cxl_get_version();
    ASSERT_NOT_NULL(version, "Version string should not be NULL");

    cxl_get_version_number(&major, &minor, &patch);
    ASSERT_EQ(major, 1, "Major version should be 1");
    ASSERT_EQ(minor, 0, "Minor version should be 0");
    ASSERT_EQ(patch, 0, "Patch version should be 0");

    TEST_PASS();
}

/* Test: Multiple init/cleanup cycles */
TEST(init_cleanup_cycles) {
    cxl_context_t *ctx;
    int i;

    for (i = 0; i < 10; i++) {
        ctx = cxl_init();
        ASSERT_NOT_NULL(ctx, "Context should not be NULL on each init");
        cxl_cleanup(ctx);
    }

    TEST_PASS();
}

/* Test: NULL context handling */
TEST(null_context_handling) {
    /* Should not crash */
    cxl_cleanup(NULL);

    int error = cxl_get_last_error(NULL);
    ASSERT_NEQ(error, 0, "Should return error for NULL context");

    TEST_PASS();
}

TEST_MAIN_BEGIN()
    TEST_SUITE_BEGIN("libcxl Basic Tests")
    RUN_TEST(library_init);
    RUN_TEST(version_info);
    RUN_TEST(init_cleanup_cycles);
    RUN_TEST(null_context_handling);
    TEST_SUITE_END()
TEST_MAIN_END()
