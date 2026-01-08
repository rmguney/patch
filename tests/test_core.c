#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include <stdio.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  %s... ", #name); \
    if (test_##name()) { g_tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { printf("ASSERT FAILED: %s\n", #cond); return 0; } } while(0)

TEST(rng_repeatability)
{
    RngState rng1, rng2;
    rng_seed(&rng1, 0x12345678);
    rng_seed(&rng2, 0x12345678);

    for (int i = 0; i < 1000; i++)
    {
        uint64_t a = rng_next(&rng1);
        uint64_t b = rng_next(&rng2);
        ASSERT(a == b);
    }
    return 1;
}

TEST(rng_different_seeds)
{
    RngState rng1, rng2;
    rng_seed(&rng1, 0x12345678);
    rng_seed(&rng2, 0x87654321);

    int same_count = 0;
    for (int i = 0; i < 100; i++)
    {
        if (rng_next(&rng1) == rng_next(&rng2))
            same_count++;
    }
    ASSERT(same_count < 5);
    return 1;
}

TEST(rng_range_bounds)
{
    RngState rng;
    rng_seed(&rng, 0xDEADBEEF);

    for (int i = 0; i < 1000; i++)
    {
        float f = rng_range_f32(&rng, 1.0f, 5.0f);
        ASSERT(f >= 1.0f && f <= 5.0f);

        uint32_t u = rng_range_u32(&rng, 10);
        ASSERT(u < 10);
    }
    return 1;
}

TEST(rng_sequence_determinism)
{
    RngState rng1;
    rng_seed(&rng1, 0xCAFEBABE);

    uint64_t expected[5];
    expected[0] = rng_next(&rng1);
    expected[1] = rng_next(&rng1);
    expected[2] = rng_next(&rng1);
    expected[3] = rng_next(&rng1);
    expected[4] = rng_next(&rng1);

    RngState rng2;
    rng_seed(&rng2, 0xCAFEBABE);

    for (int i = 0; i < 5; i++)
    {
        ASSERT(rng_next(&rng2) == expected[i]);
    }
    return 1;
}

TEST(math_clamp)
{
    ASSERT(clampf(5.0f, 0.0f, 10.0f) == 5.0f);
    ASSERT(clampf(-5.0f, 0.0f, 10.0f) == 0.0f);
    ASSERT(clampf(15.0f, 0.0f, 10.0f) == 10.0f);
    return 1;
}

TEST(math_lerp)
{
    ASSERT(fabsf(lerpf(0.0f, 10.0f, 0.5f) - 5.0f) < 0.0001f);
    ASSERT(fabsf(lerpf(0.0f, 10.0f, 0.0f) - 0.0f) < 0.0001f);
    ASSERT(fabsf(lerpf(0.0f, 10.0f, 1.0f) - 10.0f) < 0.0001f);
    return 1;
}

TEST(vec3_operations)
{
    Vec3 a = vec3_create(1.0f, 2.0f, 3.0f);
    Vec3 b = vec3_create(4.0f, 5.0f, 6.0f);

    Vec3 sum = vec3_add(a, b);
    ASSERT(fabsf(sum.x - 5.0f) < 0.0001f);
    ASSERT(fabsf(sum.y - 7.0f) < 0.0001f);
    ASSERT(fabsf(sum.z - 9.0f) < 0.0001f);

    Vec3 diff = vec3_sub(b, a);
    ASSERT(fabsf(diff.x - 3.0f) < 0.0001f);
    ASSERT(fabsf(diff.y - 3.0f) < 0.0001f);
    ASSERT(fabsf(diff.z - 3.0f) < 0.0001f);

    float dot = vec3_dot(a, b);
    ASSERT(fabsf(dot - 32.0f) < 0.0001f);

    return 1;
}

TEST(frustum_extraction)
{
    /* Create a simple perspective projection looking down -Z */
    Mat4 proj = mat4_perspective(K_PI * 0.5f, 1.0f, 0.1f, 100.0f);
    Mat4 view = mat4_look_at(vec3_zero(), vec3_create(0.0f, 0.0f, -1.0f), vec3_create(0.0f, 1.0f, 0.0f));
    Mat4 vp = mat4_multiply(proj, view);

    Frustum f = frustum_from_view_proj(vp);

    /* Verify all planes are normalized (length ~1) */
    for (int i = 0; i < 6; i++)
    {
        float len = sqrtf(f.planes[i].x * f.planes[i].x +
                          f.planes[i].y * f.planes[i].y +
                          f.planes[i].z * f.planes[i].z);
        ASSERT(fabsf(len - 1.0f) < 0.01f);
    }

    return 1;
}

TEST(frustum_aabb_inside)
{
    /* Frustum looking down -Z */
    Mat4 proj = mat4_perspective(K_PI * 0.5f, 1.0f, 0.1f, 100.0f);
    Mat4 view = mat4_look_at(vec3_zero(), vec3_create(0.0f, 0.0f, -1.0f), vec3_create(0.0f, 1.0f, 0.0f));
    Mat4 vp = mat4_multiply(proj, view);
    Frustum f = frustum_from_view_proj(vp);

    /* Box clearly inside frustum (in front of camera) */
    Bounds3D inside = { -1.0f, 1.0f, -1.0f, 1.0f, -5.0f, -3.0f };
    FrustumResult result = frustum_test_aabb(&f, inside);
    ASSERT(result != FRUSTUM_OUTSIDE);

    return 1;
}

TEST(frustum_aabb_outside)
{
    /* Frustum looking down -Z */
    Mat4 proj = mat4_perspective(K_PI * 0.5f, 1.0f, 0.1f, 100.0f);
    Mat4 view = mat4_look_at(vec3_zero(), vec3_create(0.0f, 0.0f, -1.0f), vec3_create(0.0f, 1.0f, 0.0f));
    Mat4 vp = mat4_multiply(proj, view);
    Frustum f = frustum_from_view_proj(vp);

    /* Box behind camera (positive Z) - should be outside */
    Bounds3D behind = { -1.0f, 1.0f, -1.0f, 1.0f, 5.0f, 10.0f };
    FrustumResult result = frustum_test_aabb(&f, behind);
    ASSERT(result == FRUSTUM_OUTSIDE);

    /* Box far to the left - should be outside */
    Bounds3D far_left = { -100.0f, -90.0f, -1.0f, 1.0f, -5.0f, -3.0f };
    result = frustum_test_aabb(&f, far_left);
    ASSERT(result == FRUSTUM_OUTSIDE);

    return 1;
}

TEST(bounds_behind_plane_test)
{
    Vec3 plane_point = vec3_zero();
    Vec3 plane_normal = vec3_create(0.0f, 0.0f, -1.0f);

    /* Box in front (negative Z) */
    Bounds3D in_front = { -1.0f, 1.0f, -1.0f, 1.0f, -5.0f, -3.0f };
    ASSERT(!bounds_behind_plane(in_front, plane_point, plane_normal));

    /* Box behind (positive Z) */
    Bounds3D behind = { -1.0f, 1.0f, -1.0f, 1.0f, 3.0f, 5.0f };
    ASSERT(bounds_behind_plane(behind, plane_point, plane_normal));

    /* Box straddling - not entirely behind */
    Bounds3D straddle = { -1.0f, 1.0f, -1.0f, 1.0f, -2.0f, 2.0f };
    ASSERT(!bounds_behind_plane(straddle, plane_point, plane_normal));

    return 1;
}

int main(void)
{
    printf("=== Core Tests ===\n");

    RUN_TEST(rng_repeatability);
    RUN_TEST(rng_different_seeds);
    RUN_TEST(rng_range_bounds);
    RUN_TEST(rng_sequence_determinism);
    RUN_TEST(math_clamp);
    RUN_TEST(math_lerp);
    RUN_TEST(vec3_operations);

    printf("\n=== Frustum Culling Tests ===\n");
    RUN_TEST(frustum_extraction);
    RUN_TEST(frustum_aabb_inside);
    RUN_TEST(frustum_aabb_outside);
    RUN_TEST(bounds_behind_plane_test);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
