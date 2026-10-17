#include "engine/core/profile.h"
#include "engine/core/rng.h"
#include "engine/platform/platform.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/chunk.h"
#include "content/materials.h"
#include "test_common.h"
#include "test_fixtures.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Infrastructure Tests - Verify profiling APIs work correctly
 * ============================================================================ */

TEST(profile_categories_defined)
{
    ASSERT(PROFILE_COUNT > 0);
    ASSERT(PROFILE_COUNT <= 32);

    ASSERT(PROFILE_SIM_TICK >= 0);
    ASSERT(PROFILE_SIM_TICK < PROFILE_COUNT);
    ASSERT(PROFILE_FRAME_TOTAL >= 0);
    ASSERT(PROFILE_FRAME_TOTAL < PROFILE_COUNT);
    ASSERT(PROFILE_VOXEL_UPLOAD >= 0);
    ASSERT(PROFILE_VOXEL_UPLOAD < PROFILE_COUNT);

    return 1;
}

TEST(profile_names_valid)
{
    for (int i = 0; i < PROFILE_COUNT; i++)
    {
        const char *name = profile_get_name((ProfileCategory)i);
        ASSERT(name != NULL);
        ASSERT(strlen(name) > 0);
        ASSERT(strlen(name) < 64);
    }
    return 1;
}

TEST(profile_begin_end_works)
{
    profile_reset_all();

    PROFILE_BEGIN(PROFILE_SIM_TICK);
    volatile int dummy = 0;
    for (int i = 0; i < 1000; i++)
    {
        dummy += i;
    }
    (void)dummy;
    PROFILE_END(PROFILE_SIM_TICK);

    float avg_ms = profile_get_avg_ms(PROFILE_SIM_TICK);
    float max_ms = profile_get_max_ms(PROFILE_SIM_TICK);

    ASSERT(avg_ms >= 0.0f);
    ASSERT(max_ms >= 0.0f);
    ASSERT(max_ms >= avg_ms);

    return 1;
}

TEST(profile_accumulates_correctly)
{
    profile_reset_all();

    for (int i = 0; i < 10; i++)
    {
        PROFILE_BEGIN(PROFILE_SIM_PHYSICS);
        volatile int dummy = 0;
        for (int j = 0; j < 100; j++)
        {
            dummy += j;
        }
        (void)dummy;
        PROFILE_END(PROFILE_SIM_PHYSICS);
    }

    ASSERT(profile_get_sample_count(PROFILE_SIM_PHYSICS) == 10);
    ASSERT(profile_get_avg_ms(PROFILE_SIM_PHYSICS) >= 0.0f);

    return 1;
}

TEST(per_frame_categories_record_one_sample_per_frame)
{
    /* Raycasts, edit batches and object updates come from several call sites of very
     * different cost: their scopes add up to one sample per frame */
    profile_reset_all();
    int64_t freq = platform_get_frequency();

    for (int frame = 0; frame < 3; frame++)
    {
        int64_t inside = 0;
        for (int scope = 0; scope < 4; scope++)
        {
            int64_t t0 = platform_get_ticks();
            PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);
            volatile int dummy = 0;
            for (int j = 0; j < 20000; j++)
                dummy += j;
            (void)dummy;
            PROFILE_END(PROFILE_VOXEL_RAYCAST);
            inside += platform_get_ticks() - t0;
        }
        ASSERT(profile_get_sample_count(PROFILE_VOXEL_RAYCAST) == frame);
        profile_frame_end();
        ASSERT(profile_get_sample_count(PROFILE_VOXEL_RAYCAST) == frame + 1);

        /* The sample is the frame's four scopes together */
        float measured_ms = (float)inside / (float)freq * 1000.0f;
        ASSERT(profile_get_last_ms(PROFILE_VOXEL_RAYCAST) <= measured_ms);
        ASSERT(profile_get_last_ms(PROFILE_VOXEL_RAYCAST) > 0.0f);
    }

    /* A frame without such scopes adds no sample */
    profile_frame_end();
    ASSERT(profile_get_sample_count(PROFILE_VOXEL_RAYCAST) == 3);
    return 1;
}

TEST(profile_reset_works)
{
    profile_reset_all();

    PROFILE_BEGIN(PROFILE_UI_UPDATE);
    volatile int dummy = 0;
    for (int i = 0; i < 100; i++)
    {
        dummy += i;
    }
    (void)dummy;
    PROFILE_END(PROFILE_UI_UPDATE);

    float before_reset = profile_get_avg_ms(PROFILE_UI_UPDATE);
    ASSERT(before_reset > 0.0f || g_profile_slots[PROFILE_UI_UPDATE].sample_count > 0);

    profile_reset(PROFILE_UI_UPDATE);
    float after_reset = profile_get_avg_ms(PROFILE_UI_UPDATE);
    ASSERT(after_reset == 0.0f);

    return 1;
}

TEST(profile_min_keeps_zero_tick_sample)
{
    /* A scope shorter than the timer resolution records 0 ticks. That is the true
     * minimum; min_ticks == 0 used to mean "unset", so the next longer sample
     * replaced it. The ticks are fed directly: a real timer may never read 0. */
    ProfileSlot *slot = &g_profile_slots[PROFILE_UI_UPDATE];
    profile_reset(PROFILE_UI_UPDATE);
    profile_record_ticks(PROFILE_UI_UPDATE, 0);
    profile_record_ticks(PROFILE_UI_UPDATE, 5);

    ASSERT(slot->sample_count == 2);
    ASSERT(slot->max_ticks == 5);
    ASSERT(slot->min_ticks == 0);
    return 1;
}

TEST(profile_reset_all_works)
{
    for (int i = 0; i < PROFILE_COUNT; i++)
    {
        PROFILE_BEGIN((ProfileCategory)i);
        PROFILE_END((ProfileCategory)i);
    }

    profile_reset_all();

    for (int i = 0; i < PROFILE_COUNT; i++)
    {
        float ms = profile_get_avg_ms((ProfileCategory)i);
        ASSERT(ms == 0.0f);
    }

    return 1;
}

TEST(platform_timing_sane)
{
    int64_t freq = platform_get_frequency();
    ASSERT(freq > 0);
    ASSERT(freq > 1000);

    int64_t t1 = platform_get_ticks();
    volatile int dummy = 0;
    for (int i = 0; i < 10000; i++)
    {
        dummy += i;
    }
    (void)dummy;
    int64_t t2 = platform_get_ticks();

    ASSERT(t2 >= t1);

    return 1;
}

/* ============================================================================
 * Budget Enforcement Tests - Catch catastrophic performance regressions
 * These use VERY generous budgets (10x+ expected time) to avoid CI flakiness
 * ============================================================================ */

#define BUDGET_VOLUME_CREATE_MS 500.0f
#define BUDGET_EDIT_BATCH_MS 100.0f
#define BUDGET_RAYCAST_MS 50.0f
#define BUDGET_OCCUPANCY_MS 200.0f

TEST(budget_volume_create)
{
    profile_reset_all();

    VoxelVolume *vol = volume_create_dims(4, 2, 4, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);

    /* volume_create_dims records its own PROFILE_VOLUME_INIT sample; wrapping it in
     * the same category would restart that scope's timer */
    ASSERT(profile_get_sample_count(PROFILE_VOLUME_INIT) == 1);
    float ms = profile_get_avg_ms(PROFILE_VOLUME_INIT);
    printf("(%.2fms) ", ms);

    ASSERT_PERF(ms < BUDGET_VOLUME_CREATE_MS);

    volume_destroy(vol);
    return 1;
}

TEST(budget_edit_batch)
{
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);

    profile_reset_all();

    volume_edit_begin(vol);
    Vec3 center = vec3_create(0.5f, 0.5f, 0.5f);
    fixture_fill_sphere(vol, center, 0.4f, MAT_STONE);
    volume_edit_end(vol);

    /* Edit batches time themselves (PROFILE_VOXEL_EDIT), one sample per frame */
    profile_frame_end();
    ASSERT(profile_get_sample_count(PROFILE_VOXEL_EDIT) == 1);
    float ms = profile_get_avg_ms(PROFILE_VOXEL_EDIT);
    printf("(%.2fms) ", ms);

    ASSERT_PERF(ms < BUDGET_EDIT_BATCH_MS);

    volume_destroy(vol);
    return 1;
}

TEST(budget_raycast)
{
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.0f, 0.0f, 0.0f), vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    volume_edit_end(vol);

    profile_reset_all();

    Vec3 origin = vec3_create(-1.0f, 0.5f, 0.5f);
    Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 hit_pos, hit_normal;
    uint8_t hit_mat;

    for (int i = 0; i < 100; i++)
    {
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat, 0);
    }

    /* volume_raycast times itself (PROFILE_VOXEL_RAYCAST), adding up to one sample per
     * frame. Wrapping the loop in the same category would restart its timer. */
    profile_frame_end();
    ASSERT(profile_get_sample_count(PROFILE_VOXEL_RAYCAST) == 1);
    float ms = profile_get_last_ms(PROFILE_VOXEL_RAYCAST);
    printf("(%.2fms for 100) ", ms);

    ASSERT_PERF(ms < BUDGET_RAYCAST_MS);

    volume_destroy(vol);
    return 1;
}

TEST(budget_occupancy_rebuild)
{
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);

    profile_reset_all();

    PROFILE_BEGIN(PROFILE_VOXEL_OCCUPANCY);
    volume_edit_begin(vol);
    for (int i = 0; i < 1000; i++)
    {
        Vec3 pos = vec3_create(
            (float)(i % 10) * 0.1f,
            (float)((i / 10) % 10) * 0.1f,
            (float)((i / 100) % 10) * 0.1f);
        volume_edit_set(vol, pos, MAT_STONE);
    }
    volume_edit_end(vol);
    PROFILE_END(PROFILE_VOXEL_OCCUPANCY);

    float ms = profile_get_avg_ms(PROFILE_VOXEL_OCCUPANCY);
    printf("(%.2fms for 1000 edits) ", ms);

    ASSERT_PERF(ms < BUDGET_OCCUPANCY_MS);

    volume_destroy(vol);
    return 1;
}

/* ============================================================================
 * CSV Dump - Output timing data for manual analysis
 * Controlled by --dump-csv command line flag
 * ============================================================================ */

static int g_dump_csv = 0;

static void run_benchmark_suite(void)
{
    printf("\n=== Benchmark Suite ===\n");

    profile_reset_all();

    /* Volume creation, fills and raycasts record their own categories */
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);

    volume_edit_begin(vol);
    fixture_fill_sphere(vol, vec3_create(0.5f, 0.5f, 0.5f), 0.4f, MAT_STONE);
    volume_edit_end(vol);

    for (int frame = 0; frame < 100; frame++)
    {
        Vec3 origin = vec3_create(-1.0f, 0.5f, 0.5f);
        Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
        Vec3 hit_pos, hit_normal;
        uint8_t hit_mat;
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat, 0);

        PROFILE_BEGIN(PROFILE_SIM_TICK);
        volatile int dummy = 0;
        for (int i = 0; i < 1000; i++)
        {
            dummy += i;
        }
        (void)dummy;
        PROFILE_END(PROFILE_SIM_TICK);
    }

    volume_destroy(vol);
}

static void dump_csv(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f)
    {
        printf("ERROR: Could not open %s for writing\n", filename);
        return;
    }

    fprintf(f, "category,avg_ms,max_ms,sample_count\n");

    for (int i = 0; i < PROFILE_COUNT; i++)
    {
        const char *name = profile_get_name((ProfileCategory)i);
        float avg_ms = profile_get_avg_ms((ProfileCategory)i);
        float max_ms = profile_get_max_ms((ProfileCategory)i);
        int32_t samples = g_profile_slots[i].sample_count;

        fprintf(f, "%s,%.6f,%.6f,%d\n", name, avg_ms, max_ms, samples);
    }

    fclose(f);
    printf("Profiling data written to: %s\n", filename);
}

static void print_profile_summary(void)
{
    printf("\n--- Profile Summary ---\n");
    printf("%-20s %10s %10s %8s\n", "Category", "Avg(ms)", "Max(ms)", "Samples");
    printf("%-20s %10s %10s %8s\n", "--------", "-------", "-------", "-------");

    for (int i = 0; i < PROFILE_COUNT; i++)
    {
        float avg_ms = profile_get_avg_ms((ProfileCategory)i);
        float max_ms = profile_get_max_ms((ProfileCategory)i);
        int32_t samples = g_profile_slots[i].sample_count;

        if (samples > 0)
        {
            const char *name = profile_get_name((ProfileCategory)i);
            printf("%-20s %10.4f %10.4f %8d\n", name, avg_ms, max_ms, samples);
        }
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--dump-csv") == 0)
        {
            g_dump_csv = 1;
        }
    }

    platform_time_init();

    printf("=== Profiling Infrastructure Tests ===\n");
    RUN_TEST(profile_categories_defined);
    RUN_TEST(profile_names_valid);
    RUN_TEST(profile_begin_end_works);
    RUN_TEST(profile_accumulates_correctly);
    RUN_TEST(per_frame_categories_record_one_sample_per_frame);
    RUN_TEST(profile_reset_works);
    RUN_TEST(profile_min_keeps_zero_tick_sample);
    RUN_TEST(profile_reset_all_works);
    RUN_TEST(platform_timing_sane);

    printf("\n=== Budget Enforcement Tests ===\n");
    printf("(Generous budgets to catch catastrophic regressions)\n");
    RUN_TEST(budget_volume_create);
    RUN_TEST(budget_edit_batch);
    RUN_TEST(budget_raycast);
    RUN_TEST(budget_occupancy_rebuild);

    if (g_dump_csv)
    {
        run_benchmark_suite();
        print_profile_summary();
        dump_csv("profile_results.csv");
    }

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
