#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/voxel/voxel_object.h"
#include "engine/sim/detach.h"
#include "engine/platform/platform.h"
#include "content/materials.h"
#include "test_common.h"
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
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block */
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f};
    volume_edit_begin(vol);
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    /* Disabled config */
    DetachConfig cfg = detach_config_default();
    cfg.enabled = false;

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

    /* Nothing should be spawned */
    ASSERT(result.bodies_spawned == 0);
    ASSERT(obj_world->object_count == 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(floating_island_becomes_object)
{
    /*
     * Tests that floating islands are detected by connectivity analysis.
     * Uses connectivity_analyze_volume to verify island detection works.
     */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block (not touching floor) */
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {4.0f, 14.0f, 4.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);

    /* Verify block exists */
    ASSERT(volume_get_at(vol, vec3_create(2.0f, 12.0f, 2.0f)) == MAT_STONE);

    /* Analyze connectivity - should find one floating island */
    ConnectivityResult conn_result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &conn_result);

    ASSERT(conn_result.island_count >= 1);
    ASSERT(conn_result.floating_count >= 1);

    /* Find floating island and verify it can be extracted */
    const IslandInfo *floating = NULL;
    for (int32_t i = 0; i < conn_result.island_count; i++)
    {
        if (conn_result.islands[i].is_floating)
        {
            floating = &conn_result.islands[i];
            break;
        }
    }
    ASSERT(floating != NULL);
    ASSERT(floating->voxel_count > 0);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
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
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block (not touching floor, >= min_voxels) */
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f}; /* 2x2x2 = 8 voxels */
    volume_edit_begin(vol);
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    /* Verify block exists before detach */
    Vec3 check_pos = {1.0f, 11.0f, 1.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    DetachConfig cfg = detach_config_default();
    ASSERT(cfg.enabled == true);
    ASSERT(cfg.min_voxels_per_island <= 8);

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create an anchored block (touching floor) */
    Vec3 min_corner = {0.0f, 0.0f, 0.0f};
    Vec3 max_corner = {2.0f, 4.0f, 2.0f};
    volume_edit_begin(vol);
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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

TEST(small_islands_deleted)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
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
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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
    VoxelVolume *vol = volume_create(8, 8, 8, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a floating block larger than 32 voxels in X dimension */
    Vec3 min_corner = {-20.0f, 40.0f, -2.0f};
    Vec3 max_corner = {20.0f, 44.0f, 2.0f};
    volume_edit_begin(vol);
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);
    volume_edit_end(vol);

    DetachConfig cfg = detach_config_default();

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
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
        volume_fill_box(vol, min_corner, max_corner, MAT_STONE);
        volume_edit_end(vol);
    }

    DetachConfig cfg = detach_config_default();

    PlatformTime start = platform_time_now();

    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

    PlatformTime end = platform_time_now();
    float ms = platform_time_delta_seconds(start, end) * 1000.0f;

    printf("(%.3fms, %d bodies) ", ms, result.bodies_spawned);

    ASSERT(result.bodies_spawned >= 1);

    /* Budget: <5ms for detach (event-driven, not per-frame) */
    ASSERT(ms < 5.0f);

    connectivity_work_destroy(&work);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(determinism)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};

    /* Create two identical setups */
    VoxelVolume *vol1 = volume_create(2, 2, 2, bounds);
    VoxelVolume *vol2 = volume_create(2, 2, 2, bounds);
    ASSERT(vol1 != NULL && vol2 != NULL);

    VoxelObjectWorld *obj_world1 = voxel_object_world_create(bounds, vol1->voxel_size);
    VoxelObjectWorld *obj_world2 = voxel_object_world_create(bounds, vol2->voxel_size);
    ASSERT(obj_world1 != NULL && obj_world2 != NULL);

    ConnectivityWorkBuffer work1, work2;
    ASSERT(connectivity_work_init(&work1, vol1));
    ASSERT(connectivity_work_init(&work2, vol2));

    /* Create identical floating blocks */
    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {3.0f, 13.0f, 3.0f};

    volume_edit_begin(vol1);
    volume_fill_box(vol1, min_corner, max_corner, MAT_WOOD);
    volume_edit_end(vol1);

    volume_edit_begin(vol2);
    volume_fill_box(vol2, min_corner, max_corner, MAT_WOOD);
    volume_edit_end(vol2);

    DetachConfig cfg = detach_config_default();

    DetachResult result1, result2;
    detach_terrain_process(vol1, obj_world1, &cfg, &work1, &result1);
    detach_terrain_process(vol2, obj_world2, &cfg, &work2, &result2);

    /* Results must be identical */
    ASSERT(result1.islands_processed == result2.islands_processed);
    ASSERT(result1.bodies_spawned == result2.bodies_spawned);
    ASSERT(result1.voxels_removed == result2.voxels_removed);
    ASSERT(obj_world1->object_count == obj_world2->object_count);

    connectivity_work_destroy(&work1);
    connectivity_work_destroy(&work2);
    voxel_object_world_destroy(obj_world1);
    voxel_object_world_destroy(obj_world2);
    volume_destroy(vol1);
    volume_destroy(vol2);
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
    VoxelVolume *vol = volume_create(4, 4, 4, bounds);
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
    volume_fill_box(vol, col1_min, col1_max, MAT_STONE);
    volume_fill_box(vol, col2_min, col2_max, MAT_STONE);
    volume_edit_end(vol);

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
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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
    VoxelVolume *vol = volume_create(4, 4, 4, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create large grounded terrain mass */
    Vec3 terrain_min = {-30.0f, 0.0f, -30.0f};
    Vec3 terrain_max = {30.0f, 20.0f, 30.0f};
    volume_edit_begin(vol);
    volume_fill_box(vol, terrain_min, terrain_max, MAT_STONE);
    volume_edit_end(vol);

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
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);
    PlatformTime end = platform_time_now();
    float ms = platform_time_delta_seconds(start, end) * 1000.0f;

    printf("(%.3fms, %d bodies) ", ms, result.bodies_spawned);

    /* No floating islands (hole doesn't disconnect anything) */
    ASSERT(result.bodies_spawned == 0);

    /* Performance: should be fast with boundary-seeded approach */
    ASSERT(ms < 5.0f);

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
    VoxelVolume *vol = volume_create(4, 4, 4, bounds);
    ASSERT(vol != NULL);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vol->voxel_size);
    ASSERT(obj_world != NULL);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* Create a tall column connected to ground */
    Vec3 col_min = {-4.0f, 0.0f, -4.0f};
    Vec3 col_max = {4.0f, 40.0f, 4.0f};
    volume_edit_begin(vol);
    volume_fill_box(vol, col_min, col_max, MAT_STONE);
    volume_edit_end(vol);

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
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
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
    volume_fill_box(vol, col_min, col_max, MAT_STONE);
    volume_fill_box(vol, shelf_min, shelf_max, MAT_STONE);
    volume_edit_end(vol);

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
    ASSERT(vol->last_cut_boundary.count > 0);

    DetachConfig cfg = detach_config_default();
    DetachResult result;
    detach_terrain_process(vol, obj_world, &cfg, &work, &result);

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

int main(void)
{
    printf("=== Terrain Detach Tests ===\n");

    RUN_TEST(default_config);
    RUN_TEST(no_detach_when_disabled);
    RUN_TEST(floating_island_becomes_object);
    RUN_TEST(floating_island_spawns_object);
    RUN_TEST(anchored_island_stays);
    RUN_TEST(small_islands_deleted);
    RUN_TEST(large_island_split);
    RUN_TEST(detach_performance);
    RUN_TEST(determinism);
    RUN_TEST(multi_cluster_detach);
    RUN_TEST(boundary_seeded_performance);
    RUN_TEST(large_floating_island_detaches);
    RUN_TEST(destruction_then_detach);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
