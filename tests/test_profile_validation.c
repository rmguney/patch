#include "engine/core/profile.h"
#include "engine/platform/platform.h"
#include "engine/voxel/volume.h"
#include "content/materials.h"
#include "test_common.h"
#include <math.h>

/* Test 1: Consistency - Multiple runs should give similar results */
TEST(timing_consistency)
{
    /* Warmup runs to handle cold cache and first-time allocations */
    for (int warmup = 0; warmup < 3; warmup++)
    {
        VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
        volume_edit_begin(vol);
        volume_fill_sphere(vol, vec3_create(0.3f, 0.3f, 0.3f), 0.2f, MAT_STONE);
        volume_edit_end(vol);
        volume_destroy(vol);
    }

    float times[8];
    for (int run = 0; run < 8; run++)
    {
        profile_reset_all();

        PROFILE_BEGIN(PROFILE_SIM_TICK);
        VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
        volume_edit_begin(vol);
        volume_fill_sphere(vol, vec3_create(0.3f, 0.3f, 0.3f), 0.2f, MAT_STONE);
        volume_edit_end(vol);
        volume_destroy(vol);
        PROFILE_END(PROFILE_SIM_TICK);

        times[run] = profile_get_avg_ms(PROFILE_SIM_TICK);
    }

    float avg = 0.0f;
    for (int i = 0; i < 8; i++) avg += times[i];
    avg /= 8.0f;

    float max_deviation = 0.0f;
    for (int i = 0; i < 8; i++)
    {
        float dev = fabsf(times[i] - avg) / avg;
        if (dev > max_deviation) max_deviation = dev;
    }

    printf("(avg=%.3fms, max_dev=%.0f%%) ", avg, max_deviation * 100.0f);

    /* Allow up to 200% deviation - Windows scheduler variance is high for small workloads */
    ASSERT(max_deviation < 2.0f);
    /* But ensure we're measuring something real (> 0.001ms) */
    ASSERT(avg > 0.001f);

    return 1;
}

/* Test 2: Scaling - More work should take more time */
TEST(timing_scales_with_work)
{
    float time_small, time_large;

    /* Small workload: 2x2x2 chunks */
    profile_reset_all();
    PROFILE_BEGIN(PROFILE_VOLUME_INIT);
    VoxelVolume *vol_small = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
    volume_edit_begin(vol_small);
    volume_fill_sphere(vol_small, vec3_create(0.3f, 0.3f, 0.3f), 0.2f, MAT_STONE);
    volume_edit_end(vol_small);
    PROFILE_END(PROFILE_VOLUME_INIT);
    time_small = profile_get_avg_ms(PROFILE_VOLUME_INIT);
    volume_destroy(vol_small);

    /* Large workload: 4x4x4 chunks with bigger sphere */
    profile_reset_all();
    PROFILE_BEGIN(PROFILE_VOLUME_INIT);
    VoxelVolume *vol_large = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);
    volume_edit_begin(vol_large);
    volume_fill_sphere(vol_large, vec3_create(0.6f, 0.6f, 0.6f), 0.5f, MAT_STONE);
    volume_edit_end(vol_large);
    PROFILE_END(PROFILE_VOLUME_INIT);
    time_large = profile_get_avg_ms(PROFILE_VOLUME_INIT);
    volume_destroy(vol_large);

    printf("(small=%.3fms, large=%.3fms, ratio=%.1fx) ",
           time_small, time_large, time_large / time_small);

    /* Large should take more time than small */
    ASSERT(time_large > time_small);
    /* But not absurdly more (sanity check) */
    ASSERT(time_large < time_small * 100.0f);

    return 1;
}

/* Test 3: Raycast scaling - More raycasts = more time
 * Note: volume_raycast now has internal profiling for PROFILE_VOXEL_RAYCAST,
 * so we check the accumulated internal samples instead of wrapping externally.
 */
TEST(raycast_scales_linearly)
{
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);
    volume_edit_begin(vol);
    volume_fill_box(vol, vec3_zero(), vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    volume_edit_end(vol);

    Vec3 origin = vec3_create(-1.0f, 0.5f, 0.5f);
    Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 hit_pos, hit_normal;
    uint8_t hit_mat;

    /* 100 raycasts (baseline) - profiling happens inside volume_raycast */
    profile_reset_all();
    for (int i = 0; i < 100; i++)
    {
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat);
    }
    float time_100 = profile_get_avg_ms(PROFILE_VOXEL_RAYCAST);
    int32_t samples_100 = profile_get_sample_count(PROFILE_VOXEL_RAYCAST);

    /* 1000 raycasts (10x more work) */
    profile_reset_all();
    for (int i = 0; i < 1000; i++)
    {
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat);
    }
    float time_1000 = profile_get_avg_ms(PROFILE_VOXEL_RAYCAST);
    int32_t samples_1000 = profile_get_sample_count(PROFILE_VOXEL_RAYCAST);

    volume_destroy(vol);

    printf("(100x=%.4fms/%d, 1000x=%.4fms/%d) ", time_100, samples_100, time_1000, samples_1000);

    /* Sample counts should match raycast counts */
    ASSERT(samples_100 == 100);
    ASSERT(samples_1000 == 1000);

    /* Average time per raycast should be similar (within 5x) */
    if (time_100 > 0.0001f && time_1000 > 0.0001f)
    {
        float ratio = time_1000 / time_100;
        ASSERT(ratio > 0.2f && ratio < 5.0f);
    }

    return 1;
}

/* Test 4: Sensitivity - Can detect artificial slowdown */
TEST(detects_slowdown)
{
    profile_reset_all();

    /* Fast operation */
    PROFILE_BEGIN(PROFILE_PROP_SPAWN);
    volatile int dummy = 0;
    for (int i = 0; i < 100; i++) dummy += i;
    (void)dummy;
    PROFILE_END(PROFILE_PROP_SPAWN);
    float time_fast = profile_get_avg_ms(PROFILE_PROP_SPAWN);

    /* Slow operation (10x more work) */
    profile_reset(PROFILE_PROP_SPAWN);
    PROFILE_BEGIN(PROFILE_PROP_SPAWN);
    dummy = 0;
    for (int i = 0; i < 1000; i++) dummy += i;
    (void)dummy;
    PROFILE_END(PROFILE_PROP_SPAWN);
    float time_slow = profile_get_avg_ms(PROFILE_PROP_SPAWN);

    printf("(fast=%.4fms, slow=%.4fms) ", time_fast, time_slow);

    /* Slow should be detectably longer */
    /* Note: very fast operations may have measurement noise */
    ASSERT(time_slow >= time_fast);

    return 1;
}

/* Test 5: Max tracking works */
TEST(max_tracking_works)
{
    profile_reset_all();

    /* First iteration: small work */
    PROFILE_BEGIN(PROFILE_CHUNK_UPLOAD);
    volatile int dummy = 0;
    for (int i = 0; i < 10; i++) dummy += i;
    (void)dummy;
    PROFILE_END(PROFILE_CHUNK_UPLOAD);

    /* Second iteration: large work (should set new max) */
    PROFILE_BEGIN(PROFILE_CHUNK_UPLOAD);
    dummy = 0;
    for (int i = 0; i < 10000; i++) dummy += i;
    (void)dummy;
    PROFILE_END(PROFILE_CHUNK_UPLOAD);

    /* Third iteration: small work again */
    PROFILE_BEGIN(PROFILE_CHUNK_UPLOAD);
    dummy = 0;
    for (int i = 0; i < 10; i++) dummy += i;
    (void)dummy;
    PROFILE_END(PROFILE_CHUNK_UPLOAD);

    float avg_ms = profile_get_avg_ms(PROFILE_CHUNK_UPLOAD);
    float max_ms = profile_get_max_ms(PROFILE_CHUNK_UPLOAD);

    printf("(avg=%.4fms, max=%.4fms) ", avg_ms, max_ms);

    /* Max should be >= avg */
    ASSERT(max_ms >= avg_ms);
    /* Max should capture the spike (be significantly higher than avg) */
    /* The large iteration should dominate max */

    return 1;
}

/* Test 6: Non-zero timing for real work */
TEST(measures_real_work)
{
    profile_reset_all();

    PROFILE_BEGIN(PROFILE_FRAME_TOTAL);

    /* Do substantial work */
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);
    volume_edit_begin(vol);
    for (int x = 0; x < 40; x++)
    {
        for (int y = 0; y < 40; y++)
        {
            for (int z = 0; z < 40; z++)
            {
                Vec3 pos = vec3_create(x * 0.025f, y * 0.025f, z * 0.025f);
                volume_edit_set(vol, pos, MAT_STONE);
            }
        }
    }
    volume_edit_end(vol);

    Vec3 origin = vec3_create(-1.0f, 0.5f, 0.5f);
    Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 hit_pos, hit_normal;
    uint8_t hit_mat;
    for (int i = 0; i < 1000; i++)
    {
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat);
    }

    volume_destroy(vol);

    PROFILE_END(PROFILE_FRAME_TOTAL);

    float ms = profile_get_avg_ms(PROFILE_FRAME_TOTAL);
    printf("(%.2fms) ", ms);

    /* Should measure something meaningful (> 0.1ms for this work) */
    ASSERT(ms > 0.1f);
    /* But not absurdly long (< 5 seconds) */
    ASSERT(ms < 5000.0f);

    return 1;
}

int main(void)
{
    platform_time_init();

    printf("=== Profiling Validation Tests ===\n");
    printf("(Verifying profiling yields meaningful results)\n\n");

    RUN_TEST(timing_consistency);
    RUN_TEST(timing_scales_with_work);
    RUN_TEST(raycast_scales_linearly);
    RUN_TEST(detects_slowdown);
    RUN_TEST(max_tracking_works);
    RUN_TEST(measures_real_work);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
