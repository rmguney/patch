#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/voxel/voxel_object.h"
#include "engine/sim/detach.h"
#include "engine/physics/particles.h"
#include "engine/physics/rigidbody.h"
#include "engine/core/rng.h"
#include "engine/platform/platform.h"
#include "content/materials.h"
#include "test_common.h"
#include "test_fixtures.h"
#include <string.h>

TEST(default_config)
{
    DetachConfig cfg = detach_config_default();
    ASSERT(cfg.enabled == true);
    ASSERT(cfg.max_islands_per_tick > 0);
    ASSERT(cfg.min_voxels_per_island > 0);
    ASSERT(cfg.max_bodies_alive > 0);
    return 1;
}

TEST(no_detach_when_disabled)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block */
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f};
    volume_edit_begin(vol);
    fixture_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    /* Disabled config */
    DetachConfig cfg = detach_config_default();
    cfg.enabled = false;

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* Nothing should be spawned */
    ASSERT(result.bodies_spawned == 0);
    ASSERT(obj_world->object_count == 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(floating_island_detected)
{
    /* Connectivity analysis of a fresh edit finds the floating block, whole */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* 4 m cube of 0.5 m voxels, not touching the floor */
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.0f, 10.0f, 0.0f), vec3_create(4.0f, 14.0f, 4.0f), MAT_STONE);
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    ConnectivityResult conn_result;
    connectivity_analyze_dirty(vol, bounds.min_y + 0.1f, &work, &conn_result);

    ASSERT(conn_result.island_count == 1);
    ASSERT(!conn_result.truncated);
    ASSERT(conn_result.islands[0].voxel_count == 8 * 8 * 8);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(floating_island_spawns_object)
{
    /*
     * Positive test: Verify detach_terrain_process actually spawns a VoxelObject
     * when a floating island meets size requirements.
     */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block (not touching floor, >= min_voxels) */
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f}; /* 2x2x2 = 8 voxels */
    volume_edit_begin(vol);
    fixture_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    /* Verify block exists before detach */
    Vec3 check_pos = {1.0f, 11.0f, 1.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    DetachConfig cfg = detach_config_default();
    ASSERT(cfg.enabled == true);
    ASSERT(cfg.min_voxels_per_island <= 8);

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* Floating island should be spawned as an object */
    ASSERT(result.bodies_spawned >= 1);
    ASSERT(obj_world->object_count >= 1);

    /* Voxels should be removed from volume */
    ASSERT(volume_get_at(vol, check_pos) == 0);

    /* Verify spawned object has voxels */
    ASSERT(obj_world->objects[0].active);
    ASSERT(obj_world->objects[0].voxel_count > 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(anchored_island_stays)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create an anchored block (touching floor) */
    Vec3 min_corner = {0.0f, 0.0f, 0.0f};
    Vec3 max_corner = {2.0f, 4.0f, 2.0f};
    volume_edit_begin(vol);
    fixture_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* Anchored island should NOT become an object */
    ASSERT(result.bodies_spawned == 0);
    ASSERT(obj_world->object_count == 0);

    /* Voxels should still be in volume */
    Vec3 check_pos = {1.0f, 2.0f, 1.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(water_neither_anchors_nor_joins_islands)
{
    /* A stone block on a pillar, with water under its overhang down to the floor. Cut the
     * pillar and the block rests on water alone, which holds nothing up: the block and the
     * pillar's top fall as one body, without the water, which stays where it is */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds); /* 0.5 m voxels */
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.0f, 0.0f, 0.0f), vec3_create(1.0f, 10.0f, 2.0f), MAT_STONE);
    fixture_fill_box(vol, vec3_create(1.0f, 0.0f, 0.0f), vec3_create(2.0f, 10.0f, 2.0f), MAT_WATER);
    fixture_fill_box(vol, vec3_create(0.0f, 10.0f, 0.0f), vec3_create(2.0f, 12.0f, 2.0f), MAT_STONE);
    volume_edit_end(vol);
    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
    ASSERT_EQ(result.bodies_spawned, 0);

    /* Cut the pillar between 4 and 5 m up */
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.0f, 4.0f, 0.0f), vec3_create(1.0f, 5.0f, 2.0f), MAT_AIR);
    volume_edit_end(vol);
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* Pillar top 2 x 10 x 4 voxels, block 4 x 4 x 4 */
    ASSERT_EQ(result.bodies_spawned, 1);
    const VoxelObject *obj = &obj_world->objects[result.spawned_indices[0]];
    ASSERT_EQ(obj->voxel_count, 2 * 10 * 4 + 4 * 4 * 4);
    int32_t water_in_body = 0;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        water_in_body += obj->voxels[i].material == MAT_WATER;
    ASSERT_EQ(water_in_body, 0);
    ASSERT_EQ(volume_get_at(vol, vec3_create(1.25f, 2.25f, 1.0f)), MAT_WATER);
    ASSERT_EQ(volume_get_at(vol, vec3_create(1.25f, 9.75f, 1.0f)), MAT_WATER);
    ASSERT_EQ(volume_get_at(vol, vec3_create(0.25f, 11.0f, 1.0f)), MATERIAL_EMPTY);
    ASSERT_EQ(volume_get_at(vol, vec3_create(0.25f, 2.25f, 1.0f)), MAT_STONE);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(small_islands_deleted)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a tiny floating block (less than min_voxels) */
    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(0.5f, 10.5f, 0.5f), MAT_STONE);
    volume_edit_set(vol, vec3_create(1.5f, 10.5f, 0.5f), MAT_STONE);
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();
    cfg.min_voxels_per_island = 10; /* Set high threshold */

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* Small island should be deleted, not converted */
    ASSERT(result.bodies_spawned == 0);
    ASSERT(result.voxels_removed >= 2);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(large_island_split)
{
    /*
     * Tests that floating islands larger than VOBJ_GRID_SIZE (32) are subdivided
     * into multiple VoxelObjects rather than deleted.
     */
    Bounds3D bounds = {-64.0f, 64.0f, 0.0f, 128.0f, -64.0f, 64.0f};
    VoxelVolume *vol = fixture_volume_create(8, 8, 8, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block larger than 32 voxels in X dimension */
    Vec3 min_corner = {-20.0f, 40.0f, -2.0f};
    Vec3 max_corner = {20.0f, 44.0f, 2.0f};
    volume_edit_begin(vol);
    fixture_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* Oversized island: subdivided into multiple objects */
    ASSERT(result.bodies_spawned >= 2);

    /* Voxels should be cleared from terrain */
    Vec3 check_pos = {0.0f, 42.0f, 0.0f};
    ASSERT(volume_get_at(vol, check_pos) == 0);

    /* Verify spawned objects have voxels */
    int32_t total_voxels = 0;
    for (int32_t i = 0; i < obj_world->object_count; i++)
    {
        if (obj_world->objects[i].active)
            total_voxels += obj_world->objects[i].voxel_count;
    }
    ASSERT(total_voxels > 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(detach_performance)
{
    /*
     * Benchmark detach processing to ensure it stays within budget.
     */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create multiple small floating blocks */
    for (int i = 0; i < 4; i++)
    {
        Vec3 min_corner = {-4.0f + i * 4.0f, 15.0f, -1.0f};
        Vec3 max_corner = {-2.0f + i * 4.0f, 17.0f, 1.0f};
        volume_edit_begin(vol);
        fixture_fill_box(vol, min_corner, max_corner, MAT_STONE);
        volume_edit_end(vol);
    }

    DetachConfig cfg = detach_config_default();

    PlatformTime start = platform_time_now();

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    PlatformTime end = platform_time_now();
    float ms = platform_time_delta_seconds(start, end) * 1000.0f;

    printf("(%.3fms, %d bodies) ", ms, result.bodies_spawned);

    ASSERT(result.bodies_spawned >= 1);

    /* Budget: <5ms for detach (event-driven, not per-frame) */
    ASSERT_PERF(ms < 5.0f);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(detach_golden)
{
    /* A floating 3 m block of 0.5 m voxels becomes exactly one body: these voxels,
     * this mass and this position (its source box, the grid centred on it) */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.0f, 10.0f, 0.0f), vec3_create(3.0f, 13.0f, 3.0f), MAT_STONE);
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    ASSERT(result.islands_processed == 1 && result.bodies_spawned == 1);
    ASSERT(result.voxels_removed == 0 && result.particles_spawned == 0 && !result.analysis_requeued);
    ASSERT(vol->total_solid_voxels == 0);
    const VoxelObject *obj = &obj_world->objects[result.spawned_indices[0]];
    ASSERT(obj->active && obj->voxel_count == 6 * 6 * 6);
    /* 6 voxels a side sit at grid cells 13..18: the grid centre is 3 voxels past the
     * block's minimum corner */
    ASSERT_NEAR(obj->position.x, 1.5f, 1e-5f);
    ASSERT_NEAR(obj->position.y, 11.5f, 1e-5f);
    ASSERT_NEAR(obj->position.z, 1.5f, 1e-5f);
    ASSERT_NEAR(obj->total_mass, 216.0f * material_get(MAT_STONE)->density, 1e-2f);
    ASSERT(obj->render_delay == 3);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(multi_cluster_detach)
{
    /*
     * Regression test for Bug 1: multi-cluster island_ids wipe.
     * Two spatially separated floating blocks should BOTH detach,
     * not just the last one analyzed.
     */
    Bounds3D bounds = {-32.0f, 32.0f, 0.0f, 64.0f, -32.0f, 32.0f};
    VoxelVolume *vol = fixture_volume_create(4, 4, 4, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create two columns connected to ground, far apart */
    Vec3 col1_min = {-20.0f, 0.0f, -2.0f};
    Vec3 col1_max = {-16.0f, 20.0f, 2.0f};
    Vec3 col2_min = {16.0f, 0.0f, -2.0f};
    Vec3 col2_max = {20.0f, 20.0f, 2.0f};

    volume_edit_begin(vol);
    fixture_fill_box(vol, col1_min, col1_max, MAT_STONE);
    fixture_fill_box(vol, col2_min, col2_max, MAT_STONE);
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    /* Destroy the middle sections using edit_set (records cut boundary) */
    volume_edit_begin(vol);
    {
        float vs = vol->voxel_size;
        float half_vs = vs * 0.5f;
        /* Cut 1 */
        for (float sz = -2.0f; sz < 2.0f; sz += vs)
            for (float sy = 8.0f; sy < 12.0f; sy += vs)
                for (float sx = -20.0f; sx < -16.0f; sx += vs)
                    volume_edit_set(vol, vec3_create(sx + half_vs, sy + half_vs, sz + half_vs), MATERIAL_EMPTY);
        /* Cut 2 */
        for (float sz = -2.0f; sz < 2.0f; sz += vs)
            for (float sy = 8.0f; sy < 12.0f; sy += vs)
                for (float sx = 16.0f; sx < 20.0f; sx += vs)
                    volume_edit_set(vol, vec3_create(sx + half_vs, sy + half_vs, sz + half_vs), MATERIAL_EMPTY);
    }
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* BOTH floating tops should be detected and spawned */
    ASSERT(result.bodies_spawned >= 2);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(boundary_seeded_performance)
{
    /*
     * Verify boundary-seeded BFS is fast even with large terrain.
     * Destroy a small sphere in a large terrain mass — should be <5ms.
     */
    Bounds3D bounds = {-32.0f, 32.0f, 0.0f, 64.0f, -32.0f, 32.0f};
    VoxelVolume *vol = fixture_volume_create(4, 4, 4, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create large grounded terrain mass */
    Vec3 terrain_min = {-30.0f, 0.0f, -30.0f};
    Vec3 terrain_max = {30.0f, 20.0f, 30.0f};
    volume_edit_begin(vol);
    fixture_fill_box(vol, terrain_min, terrain_max, MAT_STONE);
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    /* Destroy a small hole in the middle using edit_set (records cut boundary).
     * This matches game behavior where destruction goes through volume_edit_set. */
    Vec3 hole_center = {0.0f, 10.0f, 0.0f};
    float hole_radius = 2.0f;
    volume_edit_begin(vol);
    float half_vs = vol->voxel_size * 0.5f;
    for (float sz = hole_center.z - hole_radius; sz <= hole_center.z + hole_radius; sz += vol->voxel_size)
    {
        for (float sy = hole_center.y - hole_radius; sy <= hole_center.y + hole_radius; sy += vol->voxel_size)
        {
            for (float sx = hole_center.x - hole_radius; sx <= hole_center.x + hole_radius; sx += vol->voxel_size)
            {
                float dx = sx + half_vs - hole_center.x;
                float dy = sy + half_vs - hole_center.y;
                float dz = sz + half_vs - hole_center.z;
                if (dx * dx + dy * dy + dz * dz <= hole_radius * hole_radius)
                    volume_edit_set(vol, vec3_create(sx + half_vs, sy + half_vs, sz + half_vs), MATERIAL_EMPTY);
            }
        }
    }
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();

    PlatformTime start = platform_time_now();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
    PlatformTime end = platform_time_now();
    float ms = platform_time_delta_seconds(start, end) * 1000.0f;

    printf("(%.3fms, %d bodies) ", ms, result.bodies_spawned);

    /* No floating islands (hole doesn't disconnect anything) */
    ASSERT(result.bodies_spawned == 0);

    /* Performance: should be fast with boundary-seeded approach */
    ASSERT_PERF(ms < 5.0f);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(large_floating_island_detaches)
{
    /*
     * Regression test for Bug 2: large islands should not be force-anchored
     * due to stack overflow. A floating island with >1000 voxels should detach.
     */
    Bounds3D bounds = {-32.0f, 32.0f, 0.0f, 64.0f, -32.0f, 32.0f};
    VoxelVolume *vol = fixture_volume_create(4, 4, 4, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a tall column connected to ground */
    Vec3 col_min = {-4.0f, 0.0f, -4.0f};
    Vec3 col_max = {4.0f, 40.0f, 4.0f};
    volume_edit_begin(vol);
    fixture_fill_box(vol, col_min, col_max, MAT_STONE);
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    /* Destroy a thin slice near the bottom using edit_set (records cut boundary).
     * This matches game behavior where destruction goes through volume_edit_set. */
    volume_edit_begin(vol);
    {
        float vs = vol->voxel_size;
        float half_vs = vs * 0.5f;
        for (float sz = -4.0f; sz < 4.0f; sz += vs)
        {
            for (float sy = 4.0f; sy < 6.0f; sy += vs)
            {
                for (float sx = -4.0f; sx < 4.0f; sx += vs)
                {
                    volume_edit_set(vol, vec3_create(sx + half_vs, sy + half_vs, sz + half_vs), MATERIAL_EMPTY);
                }
            }
        }
    }
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* The large floating portion above the cut should be detected */
    ASSERT(result.bodies_spawned >= 1);

    /* Verify voxels above the cut are removed from terrain */
    Vec3 above_cut = {0.0f, 20.0f, 0.0f};
    ASSERT(volume_get_at(vol, above_cut) == 0);

    /* Verify ground portion still exists */
    Vec3 below_cut = {0.0f, 2.0f, 0.0f};
    ASSERT(volume_get_at(vol, below_cut) == MAT_STONE);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(destruction_then_detach)
{
    /*
     * Tests the full destruction → boundary-seeded analysis → detach flow.
     * Uses volume_edit_set to destroy voxels (not fill_box) to generate
     * cut boundary voxels for the boundary-seeded path.
     */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create an L-shaped structure: vertical column + horizontal shelf */
    Vec3 col_min = {0.0f, 0.0f, 0.0f};
    Vec3 col_max = {2.0f, 10.0f, 2.0f};
    Vec3 shelf_min = {2.0f, 8.0f, 0.0f};
    Vec3 shelf_max = {6.0f, 10.0f, 2.0f};
    volume_edit_begin(vol);
    fixture_fill_box(vol, col_min, col_max, MAT_STONE);
    fixture_fill_box(vol, shelf_min, shelf_max, MAT_STONE);
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    /* Destroy the connection between column and shelf using edit_set */
    volume_edit_begin(vol);
    for (float y = 8.0f; y < 10.0f; y += vol->voxel_size)
    {
        for (float z = 0.0f; z < 2.0f; z += vol->voxel_size)
        {
            volume_edit_set(vol, vec3_create(1.5f, y + vol->voxel_size * 0.5f,
                                              z + vol->voxel_size * 0.5f), MATERIAL_EMPTY);
        }
    }
    volume_edit_end(vol);

    /* Verify cut boundary was collected */
    ASSERT(vol->pending_cut_boundary.count > 0);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    /* The shelf should detach as a floating island */
    ASSERT(result.bodies_spawned >= 1);

    /* Verify shelf voxels removed from terrain */
    Vec3 shelf_check = {4.0f, 9.0f, 1.0f};
    ASSERT(volume_get_at(vol, shelf_check) == 0);

    /* Column base should still exist */
    Vec3 col_check = {1.0f, 2.0f, 1.0f};
    ASSERT(volume_get_at(vol, col_check) == MAT_STONE);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(small_island_spawns_particles)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ParticleSystem *psys = particle_system_create(bounds);
    ASSERT(psys != NULL);

    RngState rng;
    rng_seed(&rng, 42);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a small floating block: 2x2x2 = 8 voxels (under 64 threshold) */
    volume_edit_begin(vol);
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f};
    fixture_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    Vec3 check_pos = {1.0f, 11.0f, 1.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    int32_t initial_particle_count = psys->count;

    DetachConfig cfg = detach_config_default();
    ASSERT(cfg.particle_voxel_threshold == 64);

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, psys, &rng);

    /* Should spawn particles, not bodies */
    ASSERT(result.particles_spawned > 0);
    ASSERT(result.bodies_spawned == 0);
    ASSERT(obj_world->object_count == 0);

    /* Particles should have been added */
    ASSERT(psys->count > initial_particle_count);

    /* Voxels should be removed from volume */
    ASSERT(volume_get_at(vol, check_pos) == 0);

    connectivity_work_destroy(&work);
    particle_system_destroy(psys);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

/* Cut `pillar_count` 1 m pillars through a one-voxel layer in a single edit batch and
 * count how many floating tops detach after `passes` detach calls with at most
 * `max_per_pass` islands each (no new edits between passes). */
static int32_t cut_pillars_and_count_detached_ex(int32_t pillar_count, int32_t max_per_pass, int32_t passes)
{
    Bounds3D bounds = {-6.4f, 6.4f, 0.0f, 12.8f, -6.4f, 6.4f};
    VoxelVolume *vol = fixture_volume_create(4, 4, 4, bounds);
    if (!vol)
        return -1;
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ConnectivityWorkBuffer work;
    if (!obj_world || !connectivity_work_init(&work, vol))
        return -1;

    /* Pillars are 10x10 voxels, 50 tall, placed on whole voxels; the cut removes
     * voxel layer 20. Voxel centres are computed from integer indices so the cut
     * is complete regardless of float stepping. */
    float vs = vol->voxel_size;
    Vec3 tops[16];

    volume_edit_begin(vol);
    for (int32_t p = 0; p < pillar_count; p++)
    {
        float x0 = bounds.min_x + (float)(9 + (p % 4) * 30) * vs;
        float z0 = bounds.min_z + (float)(9 + (p / 4) * 30) * vs;
        fixture_fill_box(vol, vec3_create(x0 + 0.25f * vs, 0.25f * vs, z0 + 0.25f * vs),
                        vec3_create(x0 + 9.75f * vs, 49.75f * vs, z0 + 9.75f * vs), MAT_STONE);
        tops[p] = vec3_create(x0 + 5.5f * vs, 40.5f * vs, z0 + 5.5f * vs);
    }
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    volume_edit_begin(vol);
    for (int32_t p = 0; p < pillar_count; p++)
    {
        float x0 = bounds.min_x + (float)(9 + (p % 4) * 30) * vs;
        float z0 = bounds.min_z + (float)(9 + (p / 4) * 30) * vs;
        for (int32_t i = 0; i < 10; i++)
            for (int32_t k = 0; k < 10; k++)
                volume_edit_set(vol, vec3_create(x0 + ((float)i + 0.5f) * vs, 20.5f * vs,
                                                 z0 + ((float)k + 0.5f) * vs), MATERIAL_EMPTY);
    }
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();
    cfg.max_islands_per_tick = max_per_pass;
    DetachResult result;
    int32_t bodies = 0;
    for (int32_t pass = 0; pass < passes; pass++)
    {
        detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
        bodies += result.bodies_spawned;
    }

    int32_t detached = 0;
    for (int32_t p = 0; p < pillar_count; p++)
    {
        Vec3 stump = vec3_create(tops[p].x, 10.5f * vs, tops[p].z);
        if (volume_get_at(vol, stump) == 0)
            detached -= 100; /* anchored stump must stay */
        if (volume_get_at(vol, tops[p]) == 0)
            detached++;
    }
    printf("(%d pillars: %d tops detached, %d bodies) ", pillar_count, detached, bodies);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return detached;
}

static int32_t cut_pillars_and_count_detached(int32_t pillar_count)
{
    return cut_pillars_and_count_detached_ex(pillar_count, detach_config_default().max_islands_per_tick, 1);
}

TEST(several_cuts_in_one_batch)
{
    /* Each unvisited cut-boundary seed used to take one of the 64 island slots,
     * so the anchored stumps used them up and most floating tops were missed. */
    ASSERT(cut_pillars_and_count_detached(4) == 4);
    ASSERT(cut_pillars_and_count_detached(8) == 8);
    return 1;
}

TEST(detach_limit_keeps_pending_work)
{
    /* With a per-call island limit, the islands left over must still be found by
     * the next call instead of being dropped with the consumed pending edits. */
    ASSERT(cut_pillars_and_count_detached_ex(4, 2, 1) == 2);
    ASSERT(cut_pillars_and_count_detached_ex(4, 2, 2) == 4);
    return 1;
}

TEST(full_scene_second_detach_fast)
{
    /* Full-size scene volume (48x12x48 chunks). The island-id buffer is
     * volume-sized (~1.8 GB) and used to be memset on every analysis: ~0.4 s per
     * detach, and the first one committed the whole buffer. Only the chunk
     * slices that received ids are cleared now. */
    Bounds3D bounds = {-76.8f, 76.8f, 0.0f, 38.4f, -76.8f, 76.8f};
    VoxelVolume *vol = fixture_volume_create(48, 12, 48, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    float vs = vol->voxel_size;
    float ms[2] = {0.0f, 0.0f};
    for (int32_t pass = 0; pass < 2; pass++)
    {
        float x0 = bounds.min_x + (float)(760 + pass * 40) * vs;
        float z0 = bounds.min_z + 760.0f * vs;
        volume_edit_begin(vol);
        fixture_fill_box(vol, vec3_create(x0 + 0.25f * vs, 0.25f * vs, z0 + 0.25f * vs),
                        vec3_create(x0 + 9.75f * vs, 49.75f * vs, z0 + 9.75f * vs), MAT_STONE);
        volume_edit_end(vol);
        volume_consume_pending_analysis(vol);

        volume_edit_begin(vol);
        for (int32_t i = 0; i < 10; i++)
            for (int32_t k = 0; k < 10; k++)
                volume_edit_set(vol, vec3_create(x0 + ((float)i + 0.5f) * vs, 20.5f * vs,
                                                 z0 + ((float)k + 0.5f) * vs), MATERIAL_EMPTY);
        volume_edit_end(vol);

        DetachConfig cfg = detach_config_default();
        DetachResult result;
        PlatformTime t0 = platform_time_now();
        detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
        PlatformTime t1 = platform_time_now();
        ms[pass] = platform_time_delta_seconds(t0, t1) * 1000.0f;
        ASSERT(result.bodies_spawned == 1);
    }

    printf("(first=%.1fms, second=%.1fms) ", ms[0], ms[1]);
    ASSERT_PERF(ms[1] < 50.0f);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

/* Detach an 80-voxel bar (split into 32-wide bodies) with the work buffer's visited
 * generation starting at `generation`: the bodies must hold exactly the bar's voxels */
static int split_bar_from_generation(uint16_t generation)
{
    Bounds3D bounds = {0.0f, 12.8f, 0.0f, 6.4f, 0.0f, 6.4f};
    VoxelVolume *vol = fixture_volume_create(4, 2, 2, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    float vs = vol->voxel_size;
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(8.25f * vs, 30.25f * vs, 10.25f * vs),
                    vec3_create(87.75f * vs, 33.75f * vs, 13.75f * vs), MAT_STONE);
    volume_edit_end(vol);
    ASSERT(vol->total_solid_voxels == 80 * 4 * 4);

    work.generation = generation;
    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    int32_t body_voxels = 0;
    for (int32_t i = 0; i < obj_world->object_count; i++)
        if (obj_world->objects[i].active)
            body_voxels += obj_world->objects[i].voxel_count;
    printf("(from %u: %d bodies, %d voxels) ", (unsigned)generation, result.bodies_spawned, body_voxels);

    ASSERT(result.bodies_spawned >= 3);
    ASSERT(body_voxels == 80 * 4 * 4);
    ASSERT(vol->total_solid_voxels == 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(oversized_split_survives_generation_wrap)
{
    /* The analysis takes one visited generation, the split takes more. Each body's BFS
     * used to take a fresh one; when the 16-bit counter wrapped mid-split (starting at
     * UINT16_MAX - 4 or - 3), the stamp reset erased the "already extracted" marks and
     * later bodies copied voxels of earlier ones. Every start near the wrap puts it
     * somewhere else: in the analysis, between it and the split, or inside the split. */
    for (int32_t back = 4; back >= 0; back--)
        ASSERT(split_bar_from_generation((uint16_t)(UINT16_MAX - back)));
    return 1;
}

TEST(detach_beyond_64_edited_chunks)
{
    /* One batch cuts 100 pillars, one per chunk, with fill_box (no cut boundary, so
     * analysis scans the edited chunks). The pending chunk list holds 64; the rest
     * used to be dropped, leaving 36 tops floating for good. */
    Bounds3D bounds = {0.0f, 32.0f, 0.0f, 6.4f, 0.0f, 32.0f};
    VoxelVolume *vol = fixture_volume_create(10, 2, 10, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    float vs = vol->voxel_size;
    float chunk_size = vs * CHUNK_SIZE;
    volume_edit_begin(vol);
    for (int32_t p = 0; p < 100; p++)
    {
        float x0 = (float)(p % 10) * chunk_size + 14.0f * vs;
        float z0 = (float)(p / 10) * chunk_size + 14.0f * vs;
        fixture_fill_box(vol, vec3_create(x0 + 0.25f * vs, 0.25f * vs, z0 + 0.25f * vs),
                        vec3_create(x0 + 3.75f * vs, 49.75f * vs, z0 + 3.75f * vs), MAT_STONE);
    }
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);

    volume_edit_begin(vol);
    for (int32_t p = 0; p < 100; p++)
    {
        float x0 = (float)(p % 10) * chunk_size + 14.0f * vs;
        float z0 = (float)(p / 10) * chunk_size + 14.0f * vs;
        fixture_fill_box(vol, vec3_create(x0 + 0.25f * vs, 20.25f * vs, z0 + 0.25f * vs),
                        vec3_create(x0 + 3.75f * vs, 20.75f * vs, z0 + 3.75f * vs), MATERIAL_EMPTY);
    }
    volume_edit_end(vol);

    /* 64 islands fit one result; the second pass takes the rest */
    DetachConfig cfg = detach_config_default();
    DetachResult result;
    int32_t bodies = 0;
    for (int32_t pass = 0; pass < 2; pass++)
    {
        detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
        bodies += result.bodies_spawned;
    }

    int32_t tops_left = 0, stumps_left = 0;
    for (int32_t p = 0; p < 100; p++)
    {
        float x = (float)(p % 10) * chunk_size + 16.0f * vs;
        float z = (float)(p / 10) * chunk_size + 16.0f * vs;
        if (volume_get_at(vol, vec3_create(x, 40.5f * vs, z)) != 0)
            tops_left++;
        if (volume_get_at(vol, vec3_create(x, 10.5f * vs, z)) != 0)
            stumps_left++;
    }
    printf("(%d bodies, %d tops left) ", bodies, tops_left);

    ASSERT(bodies == 100);
    ASSERT(tops_left == 0);
    ASSERT(stumps_left == 100);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

/* A 9x9 column, 44 voxels (4.4 m) tall, standing on a rock slab, cut by
 * removing column layers [cut_lo, cut_hi]. With record_boundary the cut goes voxel by
 * voxel through volume_edit_set (the drill path); otherwise through fill_box, which
 * records no cut boundary (the analysis then scans the edited chunks, as after an
 * overflowed boundary). The severed top must come back as falling bodies holding
 * exactly its voxels: never left hovering, never silently deleted. */
static int severed_column_falls(bool record_boundary)
{
    Bounds3D bounds = {0.0f, 6.4f, 0.0f, 6.4f, 0.0f, 6.4f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));
    ParticleSystem *particles = particle_system_create(bounds);
    ASSERT(particles != NULL);
    RngState rng;
    rng_seed(&rng, 12345);

    const float vs = vol->voxel_size;
    const int32_t col_x = 20, col_z = 20, base_top = 5, col_h = 44, cut_lo = 3, cut_hi = 7;
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.25f * vs, 0.25f * vs, 0.25f * vs),
                    vec3_create(60.75f * vs, ((float)base_top - 0.25f) * vs, 60.75f * vs), MAT_STONE);
    fixture_fill_box(vol, vec3_create(((float)col_x + 0.25f) * vs, ((float)base_top + 0.25f) * vs, ((float)col_z + 0.25f) * vs),
                    vec3_create(((float)col_x + 8.75f) * vs, ((float)(base_top + col_h) - 0.25f) * vs, ((float)col_z + 8.75f) * vs),
                    MAT_STONE);
    volume_edit_end(vol);
    volume_consume_pending_analysis(vol);
    int32_t solid_before_cut = vol->total_solid_voxels;

    volume_edit_begin(vol);
    if (record_boundary)
    {
        for (int32_t y = base_top + cut_lo; y <= base_top + cut_hi; y++)
            for (int32_t z = col_z; z < col_z + 9; z++)
                for (int32_t x = col_x; x < col_x + 9; x++)
                    volume_edit_set(vol, vec3_create(((float)x + 0.5f) * vs, ((float)y + 0.5f) * vs, ((float)z + 0.5f) * vs),
                                    MATERIAL_EMPTY);
    }
    else
    {
        fixture_fill_box(vol, vec3_create(((float)col_x + 0.25f) * vs, ((float)(base_top + cut_lo) + 0.25f) * vs, ((float)col_z + 0.25f) * vs),
                        vec3_create(((float)col_x + 8.75f) * vs, ((float)(base_top + cut_hi) + 0.75f) * vs, ((float)col_z + 8.75f) * vs),
                        MATERIAL_EMPTY);
    }
    volume_edit_end(vol);

    const int32_t severed = 9 * 9 * (col_h - cut_hi - 1);
    ASSERT(vol->total_solid_voxels == solid_before_cut - 9 * 9 * (cut_hi - cut_lo + 1));

    /* As the game does: particles and rng given, so small pieces may turn to dust */
    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, particles, &rng);

    int32_t body_voxels = 0;
    for (int32_t i = 0; i < obj_world->object_count; i++)
        if (obj_world->objects[i].active)
            body_voxels += obj_world->objects[i].voxel_count;
    printf("(%s: %d bodies, %d of %d voxels) ", record_boundary ? "edit_set cut" : "fill_box cut",
           result.bodies_spawned, body_voxels, severed);
    ASSERT(result.bodies_spawned >= 2); /* taller than VOBJ_GRID_SIZE: split */
    ASSERT(result.particles_spawned == 0);
    ASSERT(body_voxels == severed);
    ASSERT(vol->total_solid_voxels == solid_before_cut - 9 * 9 * (cut_hi - cut_lo + 1) - severed);
    ASSERT(volume_get_at(vol, vec3_create(24.5f * vs, 40.5f * vs, 24.5f * vs)) == 0);

    /* The bodies fall onto the stump below the 0.5 m gap */
    PhysicsWorld *physics = physics_world_create(obj_world, vol);
    ASSERT(physics != NULL);
    physics_world_sync_objects(physics);
    float lowest_before = 1e30f, lowest_after = 1e30f;
    for (int32_t i = 0; i < obj_world->object_count; i++)
        if (obj_world->objects[i].active && obj_world->objects[i].position.y < lowest_before)
            lowest_before = obj_world->objects[i].position.y;
    for (int32_t step = 0; step < 60; step++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        physics_world_sync_objects(physics);
    }
    for (int32_t i = 0; i < obj_world->object_count; i++)
        if (obj_world->objects[i].active && obj_world->objects[i].position.y < lowest_after)
            lowest_after = obj_world->objects[i].position.y;
    printf("(dropped %.2f m) ", lowest_before - lowest_after);
    ASSERT(lowest_before - lowest_after > 0.3f);

    physics_world_destroy(physics);
    particle_system_destroy(particles);
    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(large_anchored_edit_finishes_in_one_pass)
{
    /* An edit without a cut boundary (or one whose boundary overflowed) is analysed
     * by seeding from every solid voxel of the edited chunks. Each seed used to take
     * an island id, so a big anchored slab used up all 65535 ids, the pass was
     * truncated and requeued, and every following pass did the same: the analysis
     * never finished. */
    Bounds3D bounds = {0.0f, 12.8f, 0.0f, 6.4f, 0.0f, 12.8f};
    VoxelVolume *vol = fixture_volume_create(4, 2, 4, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    float vs = vol->voxel_size;
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.25f * vs, 0.25f * vs, 0.25f * vs),
                    vec3_create(127.75f * vs, 19.75f * vs, 127.75f * vs), MAT_STONE);
    volume_edit_end(vol);
    ASSERT(vol->total_solid_voxels == 128 * 20 * 128);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    PlatformTime t0 = platform_time_now();
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
    float ms = platform_time_delta_seconds(t0, platform_time_now()) * 1000.0f;
    printf("(%.1fms, requeued %d) ", ms, result.analysis_requeued);

    ASSERT(!result.analysis_requeued);
    ASSERT(result.bodies_spawned == 0);
    ASSERT(vol->pending_analysis_count == 0);
    ASSERT(vol->total_solid_voxels == 128 * 20 * 128);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(severed_column_falls_as_bodies)
{
    ASSERT(severed_column_falls(true));
    ASSERT(severed_column_falls(false));
    return 1;
}

TEST(object_hit_that_removes_nothing_queues_nothing)
{
    /* A hit whose radius reaches no voxel centre changes nothing, so it must not
     * schedule a shape recalc and a full split flood fill for the object */
    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 8.0f, -8.0f, 8.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    int32_t idx = fixture_add_box(world, vec3_create(0.0f, 4.0f, 0.0f),
                                             vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(idx >= 0);
    int32_t voxels = world->objects[idx].voxel_count;

    int32_t destroyed = detach_object_at_point(world, idx, vec3_create(3.0f, 4.0f, 0.0f), 0.2f,
                                               NULL, NULL, 0);
    ASSERT(destroyed == 0);
    ASSERT(world->objects[idx].voxel_count == voxels);
    ASSERT(world->first_dirty == -1);
    ASSERT(world->split_queue_head == world->split_queue_tail);

    /* A real hit still removes voxels and schedules the follow-up work */
    destroyed = detach_object_at_point(world, idx, vec3_create(0.5f, 4.0f, 0.0f), 0.2f,
                                       NULL, NULL, 0);
    printf("(%d destroyed) ", destroyed);
    ASSERT(destroyed > 0);
    ASSERT(world->objects[idx].voxel_count == voxels - destroyed);
    ASSERT(world->first_dirty == idx);
    ASSERT(world->split_queue_head != world->split_queue_tail);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(island_at_body_limit_becomes_debris)
{
    /* With max_bodies_alive reached, a loose island was skipped and the analysis
     * requeued, so the game re-ran it every frame until bodies were freed while the
     * island hovered. It crumbles to debris now, and nothing stays pending. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    for (int32_t i = 0; i < 2; i++)
        ASSERT(fixture_add_box(obj_world, vec3_create(-10.0f + 4.0f * (float)i, 20.0f, -10.0f),
                                          vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE) >= 0);

    /* 5x5x5 floating block: above the particle threshold, so it would become a body */
    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(0.0f, 10.0f, 0.0f), vec3_create(2.5f, 12.5f, 2.5f), MAT_STONE);
    volume_edit_end(vol);
    Vec3 inside = vec3_create(1.25f, 11.25f, 1.25f);
    ASSERT(volume_get_at(vol, inside) == MAT_STONE);

    DetachConfig cfg = detach_config_default();
    cfg.max_bodies_alive = 2;
    ASSERT(cfg.particle_voxel_threshold < 125);
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);

    printf("(requeued %d, skipped %d, removed %d) ", result.analysis_requeued, result.islands_skipped,
           result.voxels_removed);
    ASSERT(result.bodies_spawned == 0);
    ASSERT(result.islands_skipped == 1);
    ASSERT(result.voxels_removed == 125);
    ASSERT(!result.analysis_requeued);
    ASSERT(vol->pending_analysis_count == 0);
    ASSERT(volume_get_at(vol, inside) == 0);
    ASSERT(vol->total_solid_voxels == 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

static Vec3 voxel_centre(const VoxelVolume *vol, int32_t x, int32_t y, int32_t z)
{
    float vs = vol->voxel_size;
    return vec3_create(vol->bounds.min_x + ((float)x + 0.5f) * vs, vol->bounds.min_y + ((float)y + 0.5f) * vs,
                       vol->bounds.min_z + ((float)z + 0.5f) * vs);
}

/* Fills voxels [lo, hi] (inclusive grid coordinates) with one material */
static void fill_voxels(VoxelVolume *vol, int32_t lo_x, int32_t lo_y, int32_t lo_z,
                        int32_t hi_x, int32_t hi_y, int32_t hi_z, uint8_t material)
{
    Vec3 lo = voxel_centre(vol, lo_x, lo_y, lo_z);
    Vec3 hi = voxel_centre(vol, hi_x, hi_y, hi_z);
    float inset = vol->voxel_size * 0.25f;
    fixture_fill_box(vol, vec3_sub(lo, vec3_create(inset, inset, inset)), vec3_add(hi, vec3_create(inset, inset, inset)),
                     material);
}

TEST(terrain_carve_cuts_lattice_sphere)
{
    /* Exactly the lattice cells within the radius go, the six axis tips included: the
     * scripted carve this replaced stepped float offsets, whose rounding pushed the tips
     * past the radius (117 of 123 cells at radius 3) */
    VoxelVolume *vol = volume_create_dims(1, 1, 1, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    fill_voxels(vol, 0, 0, 0, 31, 31, 31, MAT_STONE);
    ASSERT_EQ(vol->total_solid_voxels, 32 * 32 * 32);

    const int32_t c = 16;
    Vec3 positions[128];
    uint8_t materials[128];
    int32_t removed = detach_terrain_at_point(vol, voxel_centre(vol, c, c, c), 3, positions, materials, 128);
    ASSERT_EQ(removed, 123);
    ASSERT_EQ(vol->total_solid_voxels, 32 * 32 * 32 - 123);
    for (int32_t x = c - 4; x <= c + 4; x++)
        for (int32_t y = c - 4; y <= c + 4; y++)
            for (int32_t z = c - 4; z <= c + 4; z++)
            {
                int32_t d2 = (x - c) * (x - c) + (y - c) * (y - c) + (z - c) * (z - c);
                ASSERT_EQ(volume_get_at(vol, voxel_centre(vol, x, y, z)) == MATERIAL_EMPTY, d2 <= 9);
            }
    for (int32_t i = 0; i < removed; i++)
    {
        ASSERT_EQ(materials[i], MAT_STONE);
        ASSERT_EQ(volume_get_at(vol, positions[i]), MATERIAL_EMPTY);
    }

    /* The outputs stop at max_output, the count does not */
    removed = detach_terrain_at_point(vol, voxel_centre(vol, 6, 6, 6), 2, positions, materials, 10);
    ASSERT_EQ(removed, 33);

    volume_destroy(vol);
    return 1;
}

TEST(terrain_carve_spares_unbreakable)
{
    ASSERT(!material_is_breakable(MAT_METAL));
    ASSERT(!material_is_breakable(MAT_WATER));

    VoxelVolume *vol = volume_create_dims(1, 1, 1, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    const int32_t c = 16;
    fill_voxels(vol, 0, 0, 0, 31, 31, 31, MAT_STONE);
    fill_voxels(vol, c - 1, c - 1, c - 1, c + 1, c + 1, c + 1, MAT_METAL); /* 27 cells, all in reach */
    fill_voxels(vol, 0, c + 2, 0, 31, c + 2, 31, MAT_WATER);               /* 21 cells in reach */

    uint8_t materials[128];
    int32_t removed = detach_terrain_at_point(vol, voxel_centre(vol, c, c, c), 3, NULL, materials, 128);
    ASSERT_EQ(removed, 123 - 27 - 21);
    for (int32_t i = 0; i < removed; i++)
        ASSERT_EQ(materials[i], MAT_STONE);
    for (int32_t x = c - 1; x <= c + 1; x++)
        for (int32_t y = c - 1; y <= c + 1; y++)
            for (int32_t z = c - 1; z <= c + 1; z++)
                ASSERT_EQ(volume_get_at(vol, voxel_centre(vol, x, y, z)), MAT_METAL);
    ASSERT_EQ(volume_get_at(vol, voxel_centre(vol, c, c + 2, c)), MAT_WATER);
    ASSERT_EQ(volume_get_at(vol, voxel_centre(vol, c + 2, c, c)), MATERIAL_EMPTY);

    volume_destroy(vol);
    return 1;
}

TEST(terrain_carve_through_column_detaches_top)
{
    /* A 3x3 stone column on the volume floor: a radius-2 carve takes its whole cross
     * section, and the carve alone queues the analysis that finds the floating top */
    VoxelVolume *vol = volume_create_dims(1, 2, 1, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    VoxelObjectWorld *obj_world = voxel_object_world_create(vol->bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    fill_voxels(vol, 15, 0, 15, 17, 49, 17, MAT_STONE);
    volume_consume_pending_analysis(vol);
    ASSERT_EQ(vol->pending_analysis_count, 0);

    /* Nothing breakable in reach: nothing removed, nothing queued */
    ASSERT_EQ(detach_terrain_at_point(vol, voxel_centre(vol, 5, 40, 5), 2, NULL, NULL, 0), 0);
    ASSERT_EQ(vol->pending_analysis_count, 0);

    /* Centre column 5 cells, the other eight 3 each */
    ASSERT_EQ(detach_terrain_at_point(vol, voxel_centre(vol, 16, 25, 16), 2, NULL, NULL, 0), 29);
    ASSERT(vol->pending_analysis_count > 0);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result, NULL, NULL);
    ASSERT_EQ(result.bodies_spawned, 1);
    ASSERT_EQ(volume_get_at(vol, voxel_centre(vol, 16, 45, 16)), MATERIAL_EMPTY);
    ASSERT_EQ(volume_get_at(vol, voxel_centre(vol, 16, 10, 16)), MAT_STONE);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

int main(void)
{
    platform_time_init();
    printf("=== Terrain Detach Tests ===\n");

    RUN_TEST(default_config);
    RUN_TEST(no_detach_when_disabled);
    RUN_TEST(floating_island_detected);
    RUN_TEST(floating_island_spawns_object);
    RUN_TEST(anchored_island_stays);
    RUN_TEST(water_neither_anchors_nor_joins_islands);
    RUN_TEST(small_islands_deleted);
    RUN_TEST(large_island_split);
    RUN_TEST(detach_performance);
    RUN_TEST(detach_golden);
    RUN_TEST(multi_cluster_detach);
    RUN_TEST(boundary_seeded_performance);
    RUN_TEST(large_floating_island_detaches);
    RUN_TEST(destruction_then_detach);
    RUN_TEST(small_island_spawns_particles);
    RUN_TEST(several_cuts_in_one_batch);
    RUN_TEST(detach_limit_keeps_pending_work);
    RUN_TEST(full_scene_second_detach_fast);
    RUN_TEST(oversized_split_survives_generation_wrap);
    RUN_TEST(detach_beyond_64_edited_chunks);
    RUN_TEST(large_anchored_edit_finishes_in_one_pass);
    RUN_TEST(severed_column_falls_as_bodies);
    RUN_TEST(object_hit_that_removes_nothing_queues_nothing);
    RUN_TEST(island_at_body_limit_becomes_debris);
    RUN_TEST(terrain_carve_cuts_lattice_sphere);
    RUN_TEST(terrain_carve_spares_unbreakable);
    RUN_TEST(terrain_carve_through_column_detaches_top);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
