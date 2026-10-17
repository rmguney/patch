#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* sysconf */
#endif
#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/sim/detach.h"
#include "engine/sim/env_particles.h"
#include "engine/sim/ui.h"
#include "engine/physics/particles.h"
#include "engine/platform/platform.h"
#include "content/materials.h"
#include "test_common.h"
#include "test_fixtures.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

/* Bytes of this process in physical memory (0 where it cannot be read) */
static size_t resident_bytes(void)
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;
    return counters.WorkingSetSize;
#else
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f)
        return 0;
    long size_pages = 0, resident_pages = 0;
    int read = fscanf(f, "%ld %ld", &size_pages, &resident_pages);
    fclose(f);
    return read == 2 ? (size_t)resident_pages * (size_t)sysconf(_SC_PAGESIZE) : 0;
#endif
}

#define FRAME_BUDGET_MS 16.667f
#define TICK_COUNT 10

static void print_timing(const char *label, float avg_ms, float max_ms, float budget_pct)
{
    const char *status = budget_pct < 50.0f ? "OK" : (budget_pct < 80.0f ? "WARN" : "HIGH");
    printf("    %-24s avg=%6.2fms  max=%6.2fms  budget=%5.1f%% [%s]\n",
           label, avg_ms, max_ms, budget_pct, status);
}

TEST(voxel_objects_max_capacity)
{
    Bounds3D bounds = {-10.0f, 10.0f, 0.0f, 5.0f, -10.0f, 10.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.05f);
    ASSERT(world != NULL);

    RngState rng;
    rng_seed(&rng, 0xDEADBEEF);

    PlatformTime t0 = platform_time_now();

    int32_t spawned = 0;
    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
    {
        float x = rng_range_f32(&rng, bounds.min_x * 0.8f, bounds.max_x * 0.8f);
        float y = rng_range_f32(&rng, 1.0f, bounds.max_y * 0.9f);
        float z = rng_range_f32(&rng, bounds.min_z * 0.8f, bounds.max_z * 0.8f);
        float radius = rng_range_f32(&rng, 0.2f, 0.4f);

        int32_t idx = fixture_add_box(world, vec3_create(x, y, z), vec3_create(radius, radius, radius), 1);
        if (idx >= 0)
            spawned++;
    }

    PlatformTime t1 = platform_time_now();
    float spawn_ms = (float)(platform_time_delta_seconds(t0, t1) * 1000.0);

    printf("\n    Spawned %d/%d objects in %.2fms (%.3fms/obj)\n",
           spawned, VOBJ_MAX_OBJECTS, spawn_ms, spawn_ms / spawned);

    voxel_object_world_destroy(world);

    ASSERT(spawned >= VOBJ_MAX_OBJECTS * 0.9);
    /* Max capacity test - verify system handles load without crashing */
    /* Performance budgets tested in combined_heavy_load with realistic counts */
    return 1;
}

TEST(particles_max_capacity)
{
    Bounds3D bounds = {-10.0f, 10.0f, 0.0f, 10.0f, -10.0f, 10.0f};
    ParticleSystem *sys = particle_system_create(bounds);
    ASSERT(sys != NULL);

    RngState rng;
    rng_seed(&rng, 0xCAFEBABE);

    PlatformTime t0 = platform_time_now();

    int32_t spawned = 0;
    while (sys->count < PARTICLE_MAX_COUNT)
    {
        Vec3 pos = vec3_create(
            rng_range_f32(&rng, bounds.min_x, bounds.max_x),
            rng_range_f32(&rng, 2.0f, bounds.max_y),
            rng_range_f32(&rng, bounds.min_z, bounds.max_z));
        Vec3 vel = vec3_create(
            rng_range_f32(&rng, -5.0f, 5.0f),
            rng_range_f32(&rng, -2.0f, 10.0f),
            rng_range_f32(&rng, -5.0f, 5.0f));
        Vec3 color = vec3_create(rng_float(&rng), rng_float(&rng), rng_float(&rng));

        particle_system_add(sys, pos, vel, color, 0.03f);
        spawned++;
    }

    PlatformTime t1 = platform_time_now();
    float spawn_ms = (float)(platform_time_delta_seconds(t0, t1) * 1000.0);

    printf("\n    Spawned %d/%d particles in %.2fms\n", spawned, PARTICLE_MAX_COUNT, spawn_ms);

    float total_tick_ms = 0.0f;
    float max_tick_ms = 0.0f;

    for (int32_t tick = 0; tick < TICK_COUNT; tick++)
    {
        PlatformTime tick_start = platform_time_now();
        particle_system_update(sys, 1.0f / 60.0f, NULL, NULL);
        PlatformTime tick_end = platform_time_now();

        float tick_ms = (float)(platform_time_delta_seconds(tick_start, tick_end) * 1000.0);
        total_tick_ms += tick_ms;
        if (tick_ms > max_tick_ms)
            max_tick_ms = tick_ms;
    }

    float avg_tick_ms = total_tick_ms / TICK_COUNT;
    float budget_pct = (avg_tick_ms / FRAME_BUDGET_MS) * 100.0f;

    print_timing("particle tick", avg_tick_ms, max_tick_ms, budget_pct);

    particle_system_destroy(sys);

    ASSERT(spawned >= PARTICLE_MAX_COUNT * 0.95);
    /* Max capacity test - verify system handles load without crashing */
    return 1;
}

TEST(destruction_burst)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 5.0f, -5.0f, 5.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.05f);
    ParticleSystem *particles = particle_system_create(bounds);
    ASSERT(world != NULL && particles != NULL);

    RngState rng;
    rng_seed(&rng, 0xABCD1234);

    int32_t num_objects = 64;
    for (int32_t i = 0; i < num_objects; i++)
    {
        float x = rng_range_f32(&rng, bounds.min_x * 0.7f, bounds.max_x * 0.7f);
        float y = rng_range_f32(&rng, 0.5f, bounds.max_y * 0.7f);
        float z = rng_range_f32(&rng, bounds.min_z * 0.7f, bounds.max_z * 0.7f);
        fixture_add_box(world, vec3_create(x, y, z), vec3_create(0.4f, 0.4f, 0.4f), 1);
    }

    printf("\n    Simulating destruction burst on %d objects...\n", num_objects);

    float total_destroy_ms = 0.0f;
    int32_t total_voxels_destroyed = 0;
    int32_t destroy_count = 0;

    Vec3 destroyed_pos[256];
    uint8_t destroyed_mat[256];

    for (int32_t tick = 0; tick < 30; tick++)
    {
        if (tick % 3 == 0)
        {
            VoxelObjectHit hit = {0};

            for (int32_t i = 0; i < world->object_count && !hit.hit; i++)
            {
                if (world->objects[i].active && world->objects[i].voxel_count > 20)
                {
                    hit.hit = true;
                    hit.object_index = i;
                    hit.impact_point = world->objects[i].position;
                }
            }

            if (hit.hit)
            {
                PlatformTime t0 = platform_time_now();

                int32_t destroyed = detach_object_at_point(
                    world, hit.object_index, hit.impact_point, 0.3f,
                    destroyed_pos, destroyed_mat, 256);

                PlatformTime t1 = platform_time_now();
                total_destroy_ms += (float)(platform_time_delta_seconds(t0, t1) * 1000.0);
                total_voxels_destroyed += destroyed;
                destroy_count++;

                int32_t particle_spawn_count = destroyed < 256 ? destroyed : 256;
                for (int32_t i = 0; i < particle_spawn_count && particles->count < PARTICLE_MAX_COUNT; i++)
                {
                    Vec3 vel = vec3_scale(vec3_sub(destroyed_pos[i], hit.impact_point), 5.0f);
                    vel.y += 3.0f;
                    particle_system_add(particles, destroyed_pos[i], vel,
                                        vec3_create(1, 0.5f, 0.2f), 0.02f);
                }
            }
        }

        particle_system_update(particles, 1.0f / 60.0f, NULL, NULL);
    }

    printf("    Destroyed %d voxels in %d bursts (%.3fms total, %.3fms/burst)\n",
           total_voxels_destroyed, destroy_count, total_destroy_ms,
           destroy_count > 0 ? total_destroy_ms / destroy_count : 0);
    printf("    Particles spawned: %d\n", particles->count);

    voxel_object_world_destroy(world);
    particle_system_destroy(particles);

    ASSERT(total_voxels_destroyed > 0);
    return 1;
}

TEST(scene_volume_commits_memory_on_write)
{
    /* The scene's 48x12x48-chunk volume holds 865 MB of voxels. Creating it ran
     * chunk_init over every chunk after calloc, which made all of it resident before a
     * single voxel was generated (156 ms). Zeroed memory is a valid empty chunk now, so
     * the pages stay untouched until something is written to them. */
    size_t before = resident_bytes();
    PlatformTime t0 = platform_time_now();
    VoxelVolume *vol = volume_create_dims(48, 12, 48, vec3_create(-76.8f, 0.0f, -76.8f), 0.1f);
    float create_ms = platform_time_delta_seconds(t0, platform_time_now()) * 1000.0f;
    ASSERT(vol != NULL);
    size_t after = resident_bytes();
    size_t chunk_bytes = (size_t)vol->total_chunks * sizeof(Chunk);
    double grown_mb = ((double)after - (double)before) / (1024.0 * 1024.0);
    printf("(%.0f MB of chunks: resident %+.1f MB, %.2f ms) ", (double)chunk_bytes / (1024.0 * 1024.0), grown_mb,
           create_ms);

    /* Writing one voxel makes one page resident, not the volume */
    volume_set_at(vol, vec3_create(0.05f, 0.05f, 0.05f), MAT_STONE);
    ASSERT(volume_get_at(vol, vec3_create(0.05f, 0.05f, 0.05f)) == MAT_STONE);

    /* Not measurable where the allocator itself writes the memory: the MSVC debug heap
     * fills every block, sanitizer allocators keep shadow memory */
#if !defined(PATCH_SANITIZERS_ENABLED) && !(defined(_WIN32) && defined(_DEBUG))
    if (before > 0 && after > 0)
        ASSERT(grown_mb < (double)chunk_bytes / (16.0 * 1024.0 * 1024.0));
    ASSERT_PERF(create_ms < 20.0f);
#endif

    volume_destroy(vol);
    return 1;
}

/* Worst-case test: bulk edit deduplication (O(1) bitmap vs O(n²) linear scan) */
TEST(bulk_edit_deduplication)
{
    VoxelVolume *vol = volume_create_dims(8, 4, 8, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);

    /* Fill volume with solid voxels */
    Vec3 min_corner = vec3_create(0.1f, 0.1f, 0.1f);
    Vec3 max_corner = vec3_create(6.3f, 3.1f, 6.3f);
    fixture_fill_box(vol, min_corner, max_corner, 1);

    RngState rng;
    rng_seed(&rng, 0x55AA55AA);

    printf("\n    Testing bulk edit with 4096 random sphere carves...\n");

    /* Worst case: many small edits that touch the same chunks repeatedly */
    const int32_t NUM_EDITS = 4096;
    const int32_t NUM_ITERATIONS = 3;

    float total_ms = 0.0f;
    int32_t total_voxels_edited = 0;

    for (int32_t iter = 0; iter < NUM_ITERATIONS; iter++)
    {
        /* Refill for each iteration */
        fixture_fill_box(vol, min_corner, max_corner, 1);

        PlatformTime t0 = platform_time_now();

        volume_edit_begin(vol);
        for (int32_t i = 0; i < NUM_EDITS; i++)
        {
            Vec3 center = vec3_create(
                rng_range_f32(&rng, 0.5f, 6.0f),
                rng_range_f32(&rng, 0.5f, 2.5f),
                rng_range_f32(&rng, 0.5f, 6.0f));
            /* Small radius = edits concentrated in same chunks */
            int32_t edited = fixture_fill_sphere(vol, center, 0.15f, 0);
            total_voxels_edited += edited;
        }
        volume_edit_end(vol);

        PlatformTime t1 = platform_time_now();
        total_ms += (float)(platform_time_delta_seconds(t0, t1) * 1000.0);
    }

    float avg_ms = total_ms / NUM_ITERATIONS;
    printf("    %d edits x %d iterations: avg=%.3fms per batch\n",
           NUM_EDITS, NUM_ITERATIONS, avg_ms);
    printf("    Total voxels edited: %d (%.1f per edit)\n",
           total_voxels_edited, (float)total_voxels_edited / (NUM_EDITS * NUM_ITERATIONS));

    volume_destroy(vol);

    /* Should complete quickly with bitmap dedup.
     * Threshold is intentionally loose to avoid flakiness on busy CI machines.
     * Without bitmap (O(n²)), this would take 50-100ms+.
     */
    ASSERT_PERF(avg_ms < 12.0f);
    return 1;
}

/* Worst-case test: dirty ring overflow recovery */
TEST(dirty_ring_overflow_recovery)
{
    /* Create volume with more chunks than VOLUME_DIRTY_RING_SIZE (64) */
    VoxelVolume *vol = volume_create_dims(8, 4, 8, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);

    /* Total chunks = 8 * 4 * 8 = 256, ring size = 64 */
    printf("\n    Volume: %d chunks, dirty ring size: %d\n",
           vol->total_chunks, VOLUME_DIRTY_RING_SIZE);

    /* Edit one voxel in every chunk in a single batch (forces ring overflow) */
    printf("    Dirtying all %d chunks to force ring overflow...\n", vol->total_chunks);

    float chunk_size = vol->voxel_size * CHUNK_SIZE;
    volume_edit_begin(vol);
    for (int32_t cz = 0; cz < vol->chunks_z; cz++)
        for (int32_t cy = 0; cy < vol->chunks_y; cy++)
            for (int32_t cx = 0; cx < vol->chunks_x; cx++)
                volume_edit_set(vol, vec3_create(((float)cx + 0.5f) * chunk_size, ((float)cy + 0.5f) * chunk_size,
                                                 ((float)cz + 0.5f) * chunk_size), 1);
    volume_edit_end(vol);

    ASSERT(vol->dirty_ring_overflow == true);
    printf("    Ring overflow triggered: %s\n", vol->dirty_ring_overflow ? "yes" : "no");

    /* Now recover over multiple frames using bitmap scan */
    int32_t frames_to_recover = 0;
    int32_t total_dirty_processed = 0;
    float total_recovery_ms = 0.0f;

    while (vol->dirty_ring_overflow || vol->dirty_count > 0)
    {
        PlatformTime frame_start = platform_time_now();
        volume_begin_frame(vol);
        PlatformTime frame_end = platform_time_now();

        total_recovery_ms += (float)(platform_time_delta_seconds(frame_start, frame_end) * 1000.0);
        total_dirty_processed += vol->dirty_count;

        int32_t dirty_indices[VOLUME_MAX_DIRTY_PER_FRAME];
        int32_t count = volume_get_dirty_chunks(vol, dirty_indices, VOLUME_MAX_DIRTY_PER_FRAME);
        volume_mark_chunks_uploaded(vol, dirty_indices, count);

        frames_to_recover++;

        /* Safety limit */
        if (frames_to_recover > 100)
            break;
    }

    printf("    Recovery: %d frames, %.3fms total (%.3fms/frame)\n",
           frames_to_recover, total_recovery_ms, total_recovery_ms / frames_to_recover);
    printf("    Dirty chunks processed: %d\n", total_dirty_processed);

    int32_t expected_chunks = vol->total_chunks;
    volume_destroy(vol);

    ASSERT(total_dirty_processed == expected_chunks);
    ASSERT_PERF(total_recovery_ms / frames_to_recover < 1.0f);
    return 1;
}

TEST(env_particles_all_expire)
{
    /* Fill the pool well past 8192 live particles. A per-tick update budget used
     * to stop at the first 8192, so the rest never aged and hung in the air. */
    EnvParticleSystem *sys = env_particles_create(7);
    ASSERT(sys != NULL);
    RngState rng;
    rng_seed(&rng, 7);
    for (int32_t i = 0; i < 12000; i++)
    {
        Vec3 pos = vec3_create(rng_range_f32(&rng, -10.0f, 10.0f), 10.0f, rng_range_f32(&rng, -10.0f, 10.0f));
        env_particles_spawn(sys, ENV_PARTICLE_SNOW, pos, env_particles_steady_velocity(sys, ENV_PARTICLE_SNOW), &rng);
    }
    printf("(%d spawned) ", sys->count);
    ASSERT(sys->count > 8192);

    /* Snow lives at most 24 s (no ground here to end it sooner) */
    double time = 0.0;
    for (int32_t step = 0; step < 300; step++)
    {
        time += 0.1;
        env_particles_update(sys, NULL, 1.0f, 0.1f, time);
    }

    int32_t still_active = 0;
    for (int32_t i = 0; i < sys->count; i++)
        if (sys->particles[i].active)
            still_active++;
    printf("(%d still active) ", still_active);

    env_particles_destroy(sys);
    ASSERT(still_active == 0);
    return 1;
}

TEST(menu_slider_click_stays_in_range)
{
    /* Clicking a slider maps the click position onto [slider_min, slider_max] */
    static const char *const labels[] = {"OFF", "LOW", "MEDIUM", "HIGH"};
    UIContext ctx;
    ui_context_init(&ctx);
    UIMenu menu;
    ui_menu_clear(&menu, "TEST");
    ui_menu_add_slider_labeled(&menu, "QUALITY", 7, 2, 1, 3, labels, 4);

    /* 1920x1080: the single item is 480 px wide at x 720..1200, y 594..630 */
    const float click_x[3] = {721.0f, 960.0f, 1199.0f};
    const int32_t expected[3] = {1, 2, 3};
    for (int32_t i = 0; i < 3; i++)
    {
        ui_context_update(&ctx, 0.016f, click_x[i], 600.0f, false);
        ui_context_update(&ctx, 0.016f, click_x[i], 600.0f, true);
        ASSERT(ui_menu_update(&ctx, &menu, 1920, 1080) == 7);
        ASSERT(menu.items[0].slider_value == expected[i]);
    }
    return 1;
}

int main(void)
{
    platform_time_init();

    printf("=== Pre-RT Stress Tests ===\n");
    printf("Limits: VOBJ_MAX=%d, PARTICLE_MAX=%d\n\n",
           VOBJ_MAX_OBJECTS, PARTICLE_MAX_COUNT);

    printf("--- Capacity Tests ---\n");
    RUN_TEST(voxel_objects_max_capacity);
    RUN_TEST(particles_max_capacity);

    printf("\n--- Combined Load Tests ---\n");
    RUN_TEST(destruction_burst);

    printf("\n--- Memory Tests ---\n");
    RUN_TEST(scene_volume_commits_memory_on_write);

    printf("\n--- Bitmap Optimization Tests ---\n");
    RUN_TEST(bulk_edit_deduplication);
    RUN_TEST(dirty_ring_overflow_recovery);

    printf("\n--- Environment Particle Tests ---\n");
    RUN_TEST(env_particles_all_expire);

    printf("\n--- Menu Input ---\n");
    RUN_TEST(menu_slider_click_stays_in_range);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
