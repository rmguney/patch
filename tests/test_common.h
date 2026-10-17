#ifndef PATCH_TEST_COMMON_H
#define PATCH_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name(void)

/* Assertions test their condition through a call: a constant one (a layout fact, ASSERT(0)
 * as a failure marker) is a valid check, and must not trip MSVC's C4127 under /WX */
static inline int test_holds(int cond) { return cond; }

#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  %s... ", #name); \
    fflush(stdout); \
    if (test_##name()) { g_tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

#define ASSERT(cond) do { \
    if (!test_holds(!!(cond))) { \
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
    if (!test_holds((a) == (b))) { \
        printf("ASSERT_EQ FAILED: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

/* Wall-clock budget check. Shared CI runners are too noisy to gate on milliseconds,
 * so a miss only prints a warning unless PATCH_STRICT_PERF=1 is set. */
static inline int test_strict_perf(void)
{
    const char *v = getenv("PATCH_STRICT_PERF");
    return v != NULL && v[0] == '1';
}

#define ASSERT_PERF(cond) do { \
    if (!test_holds(!!(cond))) { \
        if (test_strict_perf()) { \
            printf("ASSERT_PERF FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            return 0; \
        } \
        printf("[perf warning: %s (%s:%d)] ", #cond, __FILE__, __LINE__); \
    } \
} while(0)

#endif
