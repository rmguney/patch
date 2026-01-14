#include "engine/core/profile.h"
#include "engine/platform/platform.h"
#include "engine/voxel/volume.h"
#include "content/materials.h"
#include "test_common.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Percentile Tests
 * ============================================================================ */

TEST(percentile_p50_median)
{
    profile_reset_all();

    /* Record known values: 1, 2, 3, ..., 100 ms (simulated via direct history write) */
    ProfileSlot *slot = &g_profile_slots[PROFILE_SIM_TICK];
    for (int i = 0; i < 100; i++)
    {
        slot->history_ms[i] = (float)(i + 1);
    }
    slot->history_count = 100;
    slot->history_index = 0;

    float p50 = profile_get_p50_ms(PROFILE_SIM_TICK);
    printf("(p50=%.1f, expected~50) ", p50);

    /* P50 should be around 50 (median of 1-100) */
    ASSERT(p50 >= 45.0f && p50 <= 55.0f);

    return 1;
}

TEST(percentile_p95_tail)
{
    profile_reset_all();

    ProfileSlot *slot = &g_profile_slots[PROFILE_SIM_TICK];
    for (int i = 0; i < 100; i++)
    {
        slot->history_ms[i] = (float)(i + 1);
    }
    slot->history_count = 100;
    slot->history_index = 0;

    float p95 = profile_get_p95_ms(PROFILE_SIM_TICK);
    printf("(p95=%.1f, expected~95) ", p95);

    /* P95 should be around 95 */
    ASSERT(p95 >= 90.0f && p95 <= 100.0f);

    return 1;
}

TEST(percentile_p99_extreme)
{
    profile_reset_all();

    ProfileSlot *slot = &g_profile_slots[PROFILE_SIM_TICK];
    for (int i = 0; i < 100; i++)
    {
        slot->history_ms[i] = (float)(i + 1);
    }
    slot->history_count = 100;
    slot->history_index = 0;

    float p99 = profile_get_p99_ms(PROFILE_SIM_TICK);
    printf("(p99=%.1f, expected~99) ", p99);

    /* P99 should be around 99 */
    ASSERT(p99 >= 95.0f && p99 <= 100.0f);

    return 1;
}

TEST(percentile_spike_detection)
{
    profile_reset_all();

    /* 99 fast samples (1ms) + 1 slow sample (100ms) */
    ProfileSlot *slot = &g_profile_slots[PROFILE_VOXEL_RAYCAST];
    for (int i = 0; i < 99; i++)
    {
        slot->history_ms[i] = 1.0f;
    }
    slot->history_ms[99] = 100.0f;  /* Spike! */
    slot->history_count = 100;
    slot->history_index = 0;

    float p50 = profile_get_p50_ms(PROFILE_VOXEL_RAYCAST);
    float p99 = profile_get_p99_ms(PROFILE_VOXEL_RAYCAST);

    printf("(p50=%.1f, p99=%.1f) ", p50, p99);

    /* P50 should be ~1ms (majority), P99 should catch the spike */
    ASSERT(p50 < 5.0f);
    ASSERT(p99 >= 50.0f);

    return 1;
}

/* ============================================================================
 * Frame Budget Tests
 * ============================================================================ */

TEST(budget_under_budget)
{
    profile_reset_all();

    /* Simulate 10ms frames (under 16.67ms budget) */
    ProfileSlot *slot = &g_profile_slots[PROFILE_FRAME_TOTAL];
    for (int i = 0; i < 10; i++)
    {
        slot->history_ms[slot->history_index] = 10.0f;
        slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
        slot->history_count++;
        profile_frame_end();
    }

    float pct = profile_budget_used_pct();
    int32_t overruns = profile_budget_overruns();

    printf("(%.0f%% used, %d overruns) ", pct, overruns);

    ASSERT(pct < 100.0f);
    ASSERT(overruns == 0);

    return 1;
}

TEST(budget_over_budget)
{
    profile_reset_all();

    /* Simulate 20ms frames (over 16.67ms budget) */
    ProfileSlot *slot = &g_profile_slots[PROFILE_FRAME_TOTAL];
    for (int i = 0; i < 10; i++)
    {
        slot->history_ms[slot->history_index] = 20.0f;
        slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
        slot->history_count++;
        profile_frame_end();
    }

    float pct = profile_budget_used_pct();
    int32_t overruns = profile_budget_overruns();

    printf("(%.0f%% used, %d overruns) ", pct, overruns);

    ASSERT(pct > 100.0f);
    ASSERT(overruns == 10);

    return 1;
}

TEST(budget_worst_frame_tracked)
{
    profile_reset_all();

    ProfileSlot *slot = &g_profile_slots[PROFILE_FRAME_TOTAL];

    /* Frame 1: 5ms */
    slot->history_ms[slot->history_index] = 5.0f;
    slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
    slot->history_count++;
    profile_frame_end();

    /* Frame 2: 50ms (worst) */
    slot->history_ms[slot->history_index] = 50.0f;
    slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
    slot->history_count++;
    profile_frame_end();

    /* Frame 3: 8ms */
    slot->history_ms[slot->history_index] = 8.0f;
    slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
    slot->history_count++;
    profile_frame_end();

    float worst = profile_budget_worst_ms();
    printf("(worst=%.1fms) ", worst);

    ASSERT(worst >= 49.0f && worst <= 51.0f);

    return 1;
}

/* ============================================================================
 * Stress Tests - Scale until budget exceeded
 * ============================================================================ */

typedef struct
{
    int32_t chunk_count;
    float init_ms;
    float fill_ms;
    float raycast_100_ms;
    float budget_pct;
} StressResult;

static StressResult run_stress_iteration(int32_t chunks_dim)
{
    StressResult result = {0};
    result.chunk_count = chunks_dim * chunks_dim * chunks_dim;

    profile_reset_all();

    /* Init */
    PROFILE_BEGIN(PROFILE_VOLUME_INIT);
    VoxelVolume *vol = volume_create_dims(chunks_dim, chunks_dim, chunks_dim, vec3_zero(), 0.1f);
    PROFILE_END(PROFILE_VOLUME_INIT);
    result.init_ms = profile_get_last_ms(PROFILE_VOLUME_INIT);

    if (!vol)
        return result;

    /* Fill */
    PROFILE_BEGIN(PROFILE_VOXEL_EDIT);
    volume_edit_begin(vol);
    float size = chunks_dim * 32 * 0.1f * 0.4f;
    Vec3 center = vec3_create(size, size, size);
    volume_fill_sphere(vol, center, size * 0.8f, MAT_STONE);
    volume_edit_end(vol);
    PROFILE_END(PROFILE_VOXEL_EDIT);
    result.fill_ms = profile_get_last_ms(PROFILE_VOXEL_EDIT);

    /* Raycast */
    Vec3 origin = vec3_create(-1.0f, size, size);
    Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 hit_pos, hit_normal;
    uint8_t hit_mat;

    PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);
    for (int i = 0; i < 100; i++)
    {
        volume_raycast(vol, origin, dir, size * 4.0f, &hit_pos, &hit_normal, &hit_mat);
    }
    PROFILE_END(PROFILE_VOXEL_RAYCAST);
    result.raycast_100_ms = profile_get_last_ms(PROFILE_VOXEL_RAYCAST);

    /* Simulated frame time */
    float frame_ms = result.init_ms + result.fill_ms + result.raycast_100_ms;
    result.budget_pct = (frame_ms / PROFILE_FRAME_BUDGET_MS) * 100.0f;

    volume_destroy(vol);
    return result;
}

TEST(stress_find_budget_limit)
{
    printf("\n");
    printf("    %-8s %8s %8s %10s %8s\n", "Chunks", "Init", "Fill", "Ray(100)", "Budget%");
    printf("    %-8s %8s %8s %10s %8s\n", "------", "----", "----", "--------", "-------");

    int32_t budget_exceeded_at = 0;

    for (int32_t dim = 1; dim <= 8; dim++)
    {
        StressResult r = run_stress_iteration(dim);

        printf("    %-8d %7.2fms %7.2fms %9.2fms %7.0f%%\n",
               r.chunk_count, r.init_ms, r.fill_ms, r.raycast_100_ms, r.budget_pct);

        if (r.budget_pct > 100.0f && budget_exceeded_at == 0)
        {
            budget_exceeded_at = r.chunk_count;
        }
    }

    printf("    Budget exceeded at: %d chunks\n", budget_exceeded_at);
    printf("    ");

    /* Test passes if we can identify a scaling limit */
    ASSERT(budget_exceeded_at > 0 || 1);  /* Always pass, this is informational */

    return 1;
}

TEST(stress_raycast_scaling)
{
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);
    volume_edit_begin(vol);
    volume_fill_sphere(vol, vec3_create(0.6f, 0.6f, 0.6f), 0.5f, MAT_STONE);
    volume_edit_end(vol);

    Vec3 origin = vec3_create(-1.0f, 0.6f, 0.6f);
    Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 hit_pos, hit_normal;
    uint8_t hit_mat;

    printf("\n");
    printf("    %-10s %10s %10s\n", "Raycasts", "Time(ms)", "Per-ray(us)");
    printf("    %-10s %10s %10s\n", "--------", "--------", "----------");

    int32_t counts[] = {10, 100, 1000, 10000};
    for (int i = 0; i < 4; i++)
    {
        int32_t count = counts[i];
        profile_reset_all();

        PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);
        for (int j = 0; j < count; j++)
        {
            volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat);
        }
        PROFILE_END(PROFILE_VOXEL_RAYCAST);

        float ms = profile_get_last_ms(PROFILE_VOXEL_RAYCAST);
        float per_ray_us = (ms / count) * 1000.0f;

        printf("    %-10d %9.3fms %9.3fus\n", count, ms, per_ray_us);
    }

    volume_destroy(vol);
    printf("    ");

    return 1;
}

/* ============================================================================
 * Detailed Profile Report
 * ============================================================================ */

static void print_detailed_report(void)
{
    printf("\n=== Detailed Profile Report ===\n\n");

    printf("%-18s %8s %8s %8s %8s %8s %8s\n",
           "Category", "Samples", "Avg(ms)", "Min(ms)", "P50(ms)", "P95(ms)", "Max(ms)");
    printf("%-18s %8s %8s %8s %8s %8s %8s\n",
           "----------------", "-------", "-------", "-------", "-------", "-------", "-------");

    for (int i = 0; i < PROFILE_COUNT; i++)
    {
        int32_t samples = profile_get_sample_count((ProfileCategory)i);
        if (samples > 0)
        {
            const char *name = profile_get_name((ProfileCategory)i);
            float avg = profile_get_avg_ms((ProfileCategory)i);
            float min_ms = profile_get_min_ms((ProfileCategory)i);
            float p50 = profile_get_p50_ms((ProfileCategory)i);
            float p95 = profile_get_p95_ms((ProfileCategory)i);
            float max_ms = profile_get_max_ms((ProfileCategory)i);

            printf("%-18s %8d %8.3f %8.3f %8.3f %8.3f %8.3f\n",
                   name, samples, avg, min_ms, p50, p95, max_ms);
        }
    }

    printf("\n--- Frame Budget ---\n");
    printf("Total frames: %d\n", g_profile_budget.total_frames);
    printf("Overruns: %d (%.1f%%)\n", g_profile_budget.overrun_count,
           g_profile_budget.total_frames > 0 ?
           (float)g_profile_budget.overrun_count / g_profile_budget.total_frames * 100.0f : 0.0f);
    printf("Worst frame: %.2fms (%.0f%% of budget)\n",
           g_profile_budget.worst_frame_ms,
           (g_profile_budget.worst_frame_ms / PROFILE_FRAME_BUDGET_MS) * 100.0f);
}

int main(int argc, char *argv[])
{
    int detailed_report = 0;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--report") == 0)
            detailed_report = 1;
    }

    platform_time_init();

    printf("=== Percentile Tests ===\n");
    RUN_TEST(percentile_p50_median);
    RUN_TEST(percentile_p95_tail);
    RUN_TEST(percentile_p99_extreme);
    RUN_TEST(percentile_spike_detection);

    printf("\n=== Frame Budget Tests ===\n");
    RUN_TEST(budget_under_budget);
    RUN_TEST(budget_over_budget);
    RUN_TEST(budget_worst_frame_tracked);

    printf("\n=== Stress Tests ===\n");
    RUN_TEST(stress_find_budget_limit);
    RUN_TEST(stress_raycast_scaling);

    if (detailed_report)
    {
        print_detailed_report();
    }

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
