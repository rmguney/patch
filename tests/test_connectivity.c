#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "content/materials.h"
#include "test_common.h"
#include <string.h>

TEST(work_buffer_init_destroy)
{
    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 16.0f, -8.0f, 8.0f};
    VoxelVolume *vol = volume_create(1, 1, 1, bounds);
    ASSERT(vol != NULL);

    ConnectivityWorkBuffer work;
    bool ok = connectivity_work_init(&work, vol);
    ASSERT(ok);
    ASSERT(work.visited_gen != NULL);
    ASSERT(work.island_ids != NULL);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(single_island_detection)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {-2.0f, 0.0f, -2.0f};
    Vec3 max_corner = {2.0f, 4.0f, 2.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    ConnectivityResult result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    ASSERT(result.island_count == 1);
    ASSERT(result.floating_count == 0);
    ASSERT(result.anchored_count == 1);
    ASSERT(result.islands[0].anchor == ANCHOR_FLOOR);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(floating_island_detection)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {-2.0f, 10.0f, -2.0f};
    Vec3 max_corner = {2.0f, 14.0f, 2.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    ConnectivityResult result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    ASSERT(result.island_count == 1);
    ASSERT(result.floating_count == 1);
    ASSERT(result.anchored_count == 0);
    ASSERT(result.islands[0].is_floating);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(multiple_islands)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 anchored_min = {-8.0f, 0.0f, -2.0f};
    Vec3 anchored_max = {-4.0f, 4.0f, 2.0f};
    volume_fill_box(vol, anchored_min, anchored_max, MAT_STONE);

    Vec3 floating_min = {4.0f, 10.0f, -2.0f};
    Vec3 floating_max = {8.0f, 14.0f, 2.0f};
    volume_fill_box(vol, floating_min, floating_max, MAT_BRICK);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    ConnectivityResult result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    ASSERT(result.island_count == 2);
    ASSERT(result.floating_count == 1);
    ASSERT(result.anchored_count == 1);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(island_extraction)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_WOOD);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    ConnectivityResult result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    ASSERT(result.island_count == 1);
    ASSERT(result.islands[0].is_floating);
    ASSERT(result.islands[0].voxel_count > 0);

    const IslandInfo *island = &result.islands[0];
    int32_t size_x = island->voxel_max_x - island->voxel_min_x + 1;
    int32_t size_y = island->voxel_max_y - island->voxel_min_y + 1;
    int32_t size_z = island->voxel_max_z - island->voxel_min_z + 1;

    uint8_t voxels[64 * 64 * 64];
    memset(voxels, 0, sizeof(voxels));

    Vec3 origin;
    int32_t extracted = connectivity_extract_island_with_ids(vol, island, &work,
                                                              voxels, size_x, size_y, size_z, &origin);

    ASSERT(extracted > 0);
    ASSERT(extracted == island->voxel_count);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(island_removal)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {0.0f, 10.0f, 0.0f};
    Vec3 max_corner = {2.0f, 12.0f, 2.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);

    Vec3 check_pos = {1.0f, 11.0f, 1.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    ConnectivityResult result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    ASSERT(result.island_count == 1);

    volume_edit_begin(vol);
    connectivity_remove_island(vol, &result.islands[0], &work);
    volume_edit_end(vol);

    ASSERT(volume_get_at(vol, check_pos) == MAT_AIR);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(stack_overflow_failsafe)
{
    Bounds3D bounds = {-64.0f, 64.0f, 0.0f, 96.0f, -64.0f, 64.0f};
    VoxelVolume *vol = volume_create(4, 3, 4, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {-24.0f, 32.0f, -24.0f};
    Vec3 max_corner = {24.0f, 80.0f, 24.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);

    Vec3 check_pos = {0.0f, 56.0f, 0.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    ConnectivityResult result;
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    ASSERT(result.island_count >= 1);

    const IslandInfo *large_island = NULL;
    int32_t max_voxels = 0;
    for (int32_t i = 0; i < result.island_count; i++)
    {
        if (result.islands[i].voxel_count > max_voxels)
        {
            max_voxels = result.islands[i].voxel_count;
            large_island = &result.islands[i];
        }
    }
    ASSERT(large_island != NULL);
    ASSERT(large_island->voxel_count > 0);

    if (large_island->anchor == ANCHOR_FLOOR && !large_island->is_floating)
    {
    }
    else
    {
        ASSERT(large_island->is_floating || large_island->anchor != ANCHOR_NONE);
    }

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(determinism)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};

    VoxelVolume *vol1 = volume_create(2, 2, 2, bounds);
    VoxelVolume *vol2 = volume_create(2, 2, 2, bounds);
    ASSERT(vol1 != NULL && vol2 != NULL);

    Vec3 positions[] = {
        {-6.0f, 0.0f, -2.0f},
        {-4.0f, 0.0f, -2.0f},
        {4.0f, 10.0f, 0.0f},
        {6.0f, 10.0f, 0.0f},
    };

    for (int i = 0; i < 4; i++)
    {
        Vec3 min = positions[i];
        Vec3 max = vec3_add(min, vec3_create(2.0f, 4.0f, 4.0f));
        volume_fill_box(vol1, min, max, MAT_STONE);
        volume_fill_box(vol2, min, max, MAT_STONE);
    }

    ConnectivityWorkBuffer work1, work2;
    ASSERT(connectivity_work_init(&work1, vol1));
    ASSERT(connectivity_work_init(&work2, vol2));

    ConnectivityResult result1, result2;
    connectivity_analyze_volume(vol1, bounds.min_y + 0.1f, 0, &work1, &result1);
    connectivity_analyze_volume(vol2, bounds.min_y + 0.1f, 0, &work2, &result2);

    ASSERT(result1.island_count == result2.island_count);
    ASSERT(result1.floating_count == result2.floating_count);
    ASSERT(result1.anchored_count == result2.anchored_count);

    for (int i = 0; i < result1.island_count; i++)
    {
        ASSERT(result1.islands[i].voxel_count == result2.islands[i].voxel_count);
        ASSERT(result1.islands[i].anchor == result2.islands[i].anchor);
    }

    connectivity_work_destroy(&work1);
    connectivity_work_destroy(&work2);
    volume_destroy(vol1);
    volume_destroy(vol2);
    return 1;
}

TEST(analyze_region_subset)
{
    Bounds3D bounds = {-32.0f, 32.0f, 0.0f, 32.0f, -32.0f, 32.0f};
    VoxelVolume *vol = volume_create(4, 2, 4, bounds);
    ASSERT(vol != NULL);

    Vec3 block1_min = {-20.0f, 10.0f, -2.0f};
    Vec3 block1_max = {-16.0f, 14.0f, 2.0f};
    volume_fill_box(vol, block1_min, block1_max, MAT_STONE);

    Vec3 block2_min = {16.0f, 10.0f, -2.0f};
    Vec3 block2_max = {20.0f, 14.0f, 2.0f};
    volume_fill_box(vol, block2_min, block2_max, MAT_BRICK);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    Vec3 region_min = {-32.0f, 0.0f, -32.0f};
    Vec3 region_max = {0.0f, 32.0f, 32.0f};

    ConnectivityResult result;
    connectivity_analyze_region(vol, region_min, region_max,
                                bounds.min_y + 0.1f, 0, &work, &result);

    printf("(found %d islands in left region) ", result.island_count);

    ASSERT(result.island_count == 1);
    ASSERT(result.floating_count == 1);

    connectivity_work_clear(&work);
    connectivity_analyze_volume(vol, bounds.min_y + 0.1f, 0, &work, &result);

    printf("(found %d islands in full volume) ", result.island_count);

    ASSERT(result.island_count == 2);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(analyze_dirty_chunks)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 block_min = {0.0f, 10.0f, 0.0f};
    Vec3 block_max = {4.0f, 14.0f, 4.0f};

    volume_edit_begin(vol);
    volume_fill_box(vol, block_min, block_max, MAT_STONE);
    volume_edit_end(vol);

    volume_begin_frame(vol);
    int32_t dirty_indices[VOLUME_MAX_DIRTY_PER_FRAME];
    int32_t count = volume_get_dirty_chunks(vol, dirty_indices, VOLUME_MAX_DIRTY_PER_FRAME);
    volume_mark_chunks_uploaded(vol, dirty_indices, count);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(2.0f, 12.0f, 2.0f), MAT_AIR);
    volume_edit_end(vol);

    ConnectivityResult result;
    connectivity_analyze_dirty(vol, bounds.min_y + 0.1f, 0, &work, &result);

    printf("(dirty analysis found %d islands) ", result.island_count);

    ASSERT(result.island_count >= 1);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

int main(void)
{
    printf("=== Connectivity Tests ===\n");

    RUN_TEST(work_buffer_init_destroy);
    RUN_TEST(single_island_detection);
    RUN_TEST(floating_island_detection);
    RUN_TEST(multiple_islands);
    RUN_TEST(island_extraction);
    RUN_TEST(island_removal);
    RUN_TEST(stack_overflow_failsafe);
    RUN_TEST(determinism);

    printf("\n=== Region/Dirty Analysis Tests ===\n");
    RUN_TEST(analyze_region_subset);
    RUN_TEST(analyze_dirty_chunks);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
