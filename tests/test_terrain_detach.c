#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/sim/voxel_object.h"
#include "engine/sim/terrain_detach.h"
#include "content/materials.h"
#include <stdio.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name)             \
    do                             \
    {                              \
        g_tests_run++;             \
        printf("  %s... ", #name); \
        if (test_##name())         \
        {                          \
            g_tests_passed++;      \
            printf("PASS\n");      \
        }                          \
        else                       \
        {                          \
            printf("FAIL\n");      \
        }                          \
    } while (0)

#define ASSERT(cond)                              \
    do                                            \
    {                                             \
        if (!(cond))                              \
        {                                         \
            printf("ASSERT FAILED: %s\n", #cond); \
            return 0;                             \
        }                                         \
    } while (0)

TEST(default_config)
{
    TerrainDetachConfig cfg = terrain_detach_config_default();
    ASSERT(cfg.enabled == true);
    ASSERT(cfg.max_islands_per_tick > 0);
    ASSERT(cfg.max_voxels_per_island > 0);
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
    TerrainDetachConfig cfg = terrain_detach_config_default();
    cfg.enabled = false;

    RngState rng;
    rng_seed(&rng, 12345);

    TerrainDetachResult result;
    terrain_detach_process(vol, obj_world, &cfg, &work, vec3_zero(), &rng, &result);

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
     * Positive test: Verify terrain_detach_process actually spawns a VoxelObject
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

    TerrainDetachConfig cfg = terrain_detach_config_default();
    ASSERT(cfg.enabled == true);
    ASSERT(cfg.min_voxels_per_island <= 8);

    RngState rng;
    rng_seed(&rng, 12345);

    TerrainDetachResult result;
    terrain_detach_process(vol, obj_world, &cfg, &work, vec3_zero(), &rng, &result);

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

    TerrainDetachConfig cfg = terrain_detach_config_default();

    RngState rng;
    rng_seed(&rng, 12345);

    TerrainDetachResult result;
    terrain_detach_process(vol, obj_world, &cfg, &work, vec3_zero(), &rng, &result);

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

    TerrainDetachConfig cfg = terrain_detach_config_default();
    cfg.min_voxels_per_island = 10; /* Set high threshold */

    RngState rng;
    rng_seed(&rng, 12345);

    TerrainDetachResult result;
    terrain_detach_process(vol, obj_world, &cfg, &work, vec3_zero(), &rng, &result);

    /* Small island should be deleted, not converted */
    ASSERT(result.bodies_spawned == 0);
    ASSERT(result.voxels_removed >= 2);

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

    TerrainDetachConfig cfg = terrain_detach_config_default();

    RngState rng1, rng2;
    rng_seed(&rng1, 12345);
    rng_seed(&rng2, 12345);

    TerrainDetachResult result1, result2;
    terrain_detach_process(vol1, obj_world1, &cfg, &work1, vec3_zero(), &rng1, &result1);
    terrain_detach_process(vol2, obj_world2, &cfg, &work2, vec3_zero(), &rng2, &result2);

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

int main(void)
{
    printf("=== Terrain Detach Tests ===\n");

    RUN_TEST(default_config);
    RUN_TEST(no_detach_when_disabled);
    RUN_TEST(floating_island_becomes_object);
    RUN_TEST(floating_island_spawns_object);
    RUN_TEST(anchored_island_stays);
    RUN_TEST(small_islands_deleted);
    RUN_TEST(determinism);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
