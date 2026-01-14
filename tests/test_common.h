#ifndef PATCH_TEST_COMMON_H
#define PATCH_TEST_COMMON_H

#include <stdio.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name(void)

#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  %s... ", #name); \
    fflush(stdout); \
    if (test_##name()) { g_tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("ASSERT_NEAR FAILED: %f != %f (%s:%d)\n", \
               (float)(a), (float)(b), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("ASSERT_EQ FAILED: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define TEST_RESULTS() printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run)

#endif
