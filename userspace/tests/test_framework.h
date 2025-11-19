/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Simple Test Framework for libcxl
 */

#ifndef _TEST_FRAMEWORK_H
#define _TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Test statistics */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Color codes */
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_RESET   "\033[0m"

/* Test macros */
#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  Running: %s ... ", #name); \
        fflush(stdout); \
        tests_run++; \
        test_##name(); \
    } \
    static void test_##name(void)

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Assertion failed: %s\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b, message) \
    do { \
        if ((a) != (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected: %ld, Got: %ld\n", (long)(b), (long)(a)); \
            printf("    %s\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_NEQ(a, b, message) \
    do { \
        if ((a) == (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Values should not be equal: %ld\n", (long)(a)); \
            printf("    %s\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(ptr, message) \
    do { \
        if ((ptr) != NULL) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected NULL, got %p\n", (void*)(ptr)); \
            printf("    %s\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(ptr, message) \
    do { \
        if ((ptr) == NULL) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected non-NULL pointer\n"); \
            printf("    %s\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b, message) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected: \"%s\", Got: \"%s\"\n", (b), (a)); \
            printf("    %s\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define TEST_PASS() \
    do { \
        printf(COLOR_GREEN "PASS" COLOR_RESET "\n"); \
        tests_passed++; \
    } while (0)

/* Test suite management */
#define RUN_TEST(name) run_test_##name()

#define TEST_SUITE_BEGIN(suite_name) \
    printf("\n" COLOR_YELLOW "Test Suite: %s" COLOR_RESET "\n", suite_name); \
    printf("========================================\n");

#define TEST_SUITE_END() \
    printf("========================================\n"); \
    printf("Tests run:    %d\n", tests_run); \
    printf("Tests passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
    printf("Tests failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
    printf("\n");

#define TEST_MAIN_BEGIN() \
    int main(void) { \
        tests_run = 0; \
        tests_passed = 0; \
        tests_failed = 0;

#define TEST_MAIN_END() \
        if (tests_failed > 0) { \
            printf(COLOR_RED "OVERALL: FAILED" COLOR_RESET "\n"); \
            return 1; \
        } else { \
            printf(COLOR_GREEN "OVERALL: PASSED" COLOR_RESET "\n"); \
            return 0; \
        } \
    }

#endif /* _TEST_FRAMEWORK_H */
