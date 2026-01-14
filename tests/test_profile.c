#include "engine/core/profile.h"
#include "engine/core/rng.h"
#include "engine/platform/platform.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/chunk.h"
#include "content/materials.h"
#include "test_common.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Infrastructure Tests - Verify profiling APIs work correctly
 * ============================================================================ */

TEST(profile_categories_defined)
{
    ASSERT(PROFILE_COUNT > 0);
    ASSERT(PROFILE_COUNT <= 20);

    ASSERT(PROFILE_SIM_TICK >= 0);
    ASSERT(PROFILE_SIM_TICK < PROFILE_COUNT);
    ASSERT(PROFILE_FRAME_TOTAL >= 0);
    ASSERT(PROFILE_FRAME_TOTAL < PROFILE_COUNT);
    ASSERT(PROFILE_CHUNK_UPLOAD >= 0);
    ASSERT(PROFILE_CHUNK_UPLOAD < PROFILE_COUNT);

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
        PROFILE_BEGIN(PROFILE_RAYCAST);
        volatile int dummy = 0;
        for (int j = 0; j < 100; j++)
        {
            dummy += j;
        }
        (void)dummy;
        PROFILE_END(PROFILE_RAYCAST);
    }

    float avg_ms = profile_get_avg_ms(PROFILE_RAYCAST);
    ASSERT(avg_ms >= 0.0f);

    return 1;
}

TEST(profile_reset_works)
{
    profile_reset_all();

    PROFILE_BEGIN(PROFILE_PROP_SPAWN);
    volatile int dummy = 0;
    for (int i = 0; i < 100; i++)
    {
        dummy += i;
    }
    (void)dummy;
    PROFILE_END(PROFILE_PROP_SPAWN);

    float before_reset = profile_get_avg_ms(PROFILE_PROP_SPAWN);
    ASSERT(before_reset > 0.0f || g_profile_slots[PROFILE_PROP_SPAWN].sample_count > 0);

    profile_reset(PROFILE_PROP_SPAWN);
    float after_reset = profile_get_avg_ms(PROFILE_PROP_SPAWN);
    ASSERT(after_reset == 0.0f);

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

#define BUDGET_VOLUME_CREATE_MS    500.0f
#define BUDGET_CHUNK_FILL_MS       100.0f
#define BUDGET_RAYCAST_MS          50.0f
#define BUDGET_OCCUPANCY_MS        200.0f

TEST(budget_volume_create)
{
    profile_reset_all();

    PROFILE_BEGIN(PROFILE_VOLUME_INIT);
    VoxelVolume *vol = volume_create_dims(4, 2, 4, vec3_zero(), 0.1f);
    PROFILE_END(PROFILE_VOLUME_INIT);

    ASSERT(vol != NULL);

    float ms = profile_get_avg_ms(PROFILE_VOLUME_INIT);
    printf("(%.2fms) ", ms);

    ASSERT(ms < BUDGET_VOLUME_CREATE_MS);

    volume_destroy(vol);
    return 1;
}

TEST(budget_chunk_fill)
{
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);

    profile_reset_all();

    PROFILE_BEGIN(PROFILE_DRAW_VOLUME);
    volume_edit_begin(vol);
    Vec3 center = vec3_create(0.5f, 0.5f, 0.5f);
    volume_fill_sphere(vol, center, 0.4f, MAT_STONE);
    volume_edit_end(vol);
    PROFILE_END(PROFILE_DRAW_VOLUME);

    float ms = profile_get_avg_ms(PROFILE_DRAW_VOLUME);
    printf("(%.2fms) ", ms);

    ASSERT(ms < BUDGET_CHUNK_FILL_MS);

    volume_destroy(vol);
    return 1;
}

TEST(budget_raycast)
{
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);
    volume_edit_begin(vol);
    volume_fill_box(vol, vec3_create(0.0f, 0.0f, 0.0f), vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    volume_edit_end(vol);

    profile_reset_all();

    Vec3 origin = vec3_create(-1.0f, 0.5f, 0.5f);
    Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 hit_pos, hit_normal;
    uint8_t hit_mat;

    PROFILE_BEGIN(PROFILE_RAYCAST);
    for (int i = 0; i < 100; i++)
    {
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat);
    }
    PROFILE_END(PROFILE_RAYCAST);

    float ms = profile_get_avg_ms(PROFILE_RAYCAST);
    printf("(%.2fms for 100) ", ms);

    ASSERT(ms < BUDGET_RAYCAST_MS);

    volume_destroy(vol);
    return 1;
}

TEST(budget_occupancy_rebuild)
{
    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);

    profile_reset_all();

    PROFILE_BEGIN(PROFILE_OCCUPANCY_REBUILD);
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
    PROFILE_END(PROFILE_OCCUPANCY_REBUILD);

    float ms = profile_get_avg_ms(PROFILE_OCCUPANCY_REBUILD);
    printf("(%.2fms for 1000 edits) ", ms);

    ASSERT(ms < BUDGET_OCCUPANCY_MS);

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

    VoxelVolume *vol = volume_create_dims(4, 4, 4, vec3_zero(), 0.1f);

    PROFILE_BEGIN(PROFILE_VOLUME_INIT);
    volume_edit_begin(vol);
    volume_fill_sphere(vol, vec3_create(0.5f, 0.5f, 0.5f), 0.4f, MAT_STONE);
    volume_edit_end(vol);
    PROFILE_END(PROFILE_VOLUME_INIT);

    for (int frame = 0; frame < 100; frame++)
    {
        PROFILE_BEGIN(PROFILE_RAYCAST);
        Vec3 origin = vec3_create(-1.0f, 0.5f, 0.5f);
        Vec3 dir = vec3_create(1.0f, 0.0f, 0.0f);
        Vec3 hit_pos, hit_normal;
        uint8_t hit_mat;
        volume_raycast(vol, origin, dir, 10.0f, &hit_pos, &hit_normal, &hit_mat);
        PROFILE_END(PROFILE_RAYCAST);

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
    RUN_TEST(profile_reset_works);
    RUN_TEST(profile_reset_all_works);
    RUN_TEST(platform_timing_sane);

    printf("\n=== Budget Enforcement Tests ===\n");
    printf("(Generous budgets to catch catastrophic regressions)\n");
    RUN_TEST(budget_volume_create);
    RUN_TEST(budget_chunk_fill);
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
