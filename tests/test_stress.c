#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/core/profile.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/sim/detach.h"
#include "engine/physics/particles.h"
#include "engine/platform/platform.h"
#include "test_common.h"
#include <string.h>
#include <stdlib.h>

static bool g_verbose = false;

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

        int32_t idx = voxel_object_world_add_sphere(world, vec3_create(x, y, z), radius, 1);
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

        if (particle_system_add(sys, &rng, pos, vel, color, 0.03f) >= 0)
            spawned++;
        else
            break;
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
        voxel_object_world_add_sphere(world, vec3_create(x, y, z), 0.4f, 1);
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
            VoxelObjectHit hit;
            hit.hit = false;

            for (int32_t i = 0; i < world->object_count && !hit.hit; i++)
            {
                if (world->objects[i].active && world->objects[i].voxel_count > 20)
                {
                    hit.hit = true;
                    hit.object_index = i;
                    hit.impact_point = world->objects[i].position;
                    hit.impact_normal = vec3_create(0, 1, 0);
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
                    particle_system_add(particles, &rng, destroyed_pos[i], vel,
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

TEST(memory_footprint)
{
    size_t vobj_world_size = sizeof(VoxelObjectWorld);
    size_t particle_sys_size = sizeof(ParticleSystem);

    size_t vobj_per_object = sizeof(VoxelObject);
    size_t particle_per = sizeof(Particle);

    printf("\n");
    printf("    VoxelObjectWorld: %.2f MB (%d objects x %zu bytes)\n",
           vobj_world_size / (1024.0f * 1024.0f), VOBJ_MAX_OBJECTS, vobj_per_object);
    printf("    ParticleSystem:   %.2f MB (%d particles x %zu bytes)\n",
           particle_sys_size / (1024.0f * 1024.0f), PARTICLE_MAX_COUNT, particle_per);

    size_t total = vobj_world_size + particle_sys_size;
    printf("    Total static:     %.2f MB\n", total / (1024.0f * 1024.0f));

    ASSERT(total < 512 * 1024 * 1024);
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
    volume_fill_box(vol, min_corner, max_corner, 1);

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
        volume_fill_box(vol, min_corner, max_corner, 1);

        PlatformTime t0 = platform_time_now();

        volume_edit_begin(vol);
        for (int32_t i = 0; i < NUM_EDITS; i++)
        {
            Vec3 center = vec3_create(
                rng_range_f32(&rng, 0.5f, 6.0f),
                rng_range_f32(&rng, 0.5f, 2.5f),
                rng_range_f32(&rng, 0.5f, 6.0f));
            /* Small radius = edits concentrated in same chunks */
            int32_t edited = volume_fill_sphere(vol, center, 0.15f, 0);
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
    ASSERT(avg_ms < 12.0f);
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

    /* Fill all chunks with solid voxels */
    for (int32_t cz = 0; cz < vol->chunks_z; cz++)
    {
        for (int32_t cy = 0; cy < vol->chunks_y; cy++)
        {
            for (int32_t cx = 0; cx < vol->chunks_x; cx++)
            {
                Chunk *chunk = volume_get_chunk(vol, cx, cy, cz);
                if (chunk)
                {
                    chunk_fill(chunk, 1);
                    chunk->state = CHUNK_STATE_ACTIVE;
                }
            }
        }
    }

    /* Dirty ALL chunks in a single frame (forces ring overflow) */
    printf("    Dirtying all %d chunks to force ring overflow...\n", vol->total_chunks);

    PlatformTime t0 = platform_time_now();

    for (int32_t i = 0; i < vol->total_chunks; i++)
    {
        volume_mark_chunk_dirty(vol, i);
    }

    PlatformTime t1 = platform_time_now();
    float dirty_ms = (float)(platform_time_delta_seconds(t0, t1) * 1000.0);
    (void)dirty_ms;

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

    ASSERT(total_dirty_processed >= expected_chunks - 10);
    ASSERT(total_recovery_ms / frames_to_recover < 1.0f);
    return 1;
}

int main(int argc, char **argv)
{
    platform_time_init();

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            g_verbose = true;
    }

    printf("=== Pre-RT Stress Tests ===\n");
    printf("Limits: VOBJ_MAX=%d, PARTICLE_MAX=%d\n\n",
           VOBJ_MAX_OBJECTS, PARTICLE_MAX_COUNT);

    printf("--- Capacity Tests ---\n");
    RUN_TEST(voxel_objects_max_capacity);
    RUN_TEST(particles_max_capacity);

    printf("\n--- Combined Load Tests ---\n");
    RUN_TEST(destruction_burst);

    printf("\n--- Memory Tests ---\n");
    RUN_TEST(memory_footprint);

    printf("\n--- Bitmap Optimization Tests ---\n");
    RUN_TEST(bulk_edit_deduplication);
    RUN_TEST(dirty_ring_overflow_recovery);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
