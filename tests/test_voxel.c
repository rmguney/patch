#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "content/materials.h"
#include "test_common.h"
#include <string.h>

TEST(volume_create_destroy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);
    ASSERT(vol->chunks_x == 2);
    ASSERT(vol->chunks_y == 2);
    ASSERT(vol->chunks_z == 2);
    volume_destroy(vol);
    return 1;
}

TEST(volume_edit_determinism)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol1 = volume_create(2, 2, 2, bounds);
    VoxelVolume *vol2 = volume_create(2, 2, 2, bounds);
    ASSERT(vol1 != NULL && vol2 != NULL);

    Vec3 positions[] = {
        {0.5f, 1.0f, 0.5f},
        {1.5f, 2.0f, 1.5f},
        {-0.5f, 3.0f, -0.5f},
    };
    uint8_t materials[] = {MAT_STONE, MAT_BRICK, MAT_WOOD};

    volume_edit_begin(vol1);
    volume_edit_begin(vol2);
    for (int i = 0; i < 3; i++)
    {
        volume_edit_set(vol1, positions[i], materials[i]);
        volume_edit_set(vol2, positions[i], materials[i]);
    }
    volume_edit_end(vol1);
    volume_edit_end(vol2);

    for (int i = 0; i < 3; i++)
    {
        uint8_t m1 = volume_get_at(vol1, positions[i]);
        uint8_t m2 = volume_get_at(vol2, positions[i]);
        ASSERT(m1 == m2);
        ASSERT(m1 == materials[i]);
    }

    volume_destroy(vol1);
    volume_destroy(vol2);
    return 1;
}

TEST(volume_fill_box)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {-2.0f, 0.0f, -2.0f};
    Vec3 max_corner = {2.0f, 4.0f, 2.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_CONCRETE);

    Vec3 inside = {0.0f, 2.0f, 0.0f};
    Vec3 outside = {10.0f, 2.0f, 10.0f};

    uint8_t mat_inside = volume_get_at(vol, inside);
    uint8_t mat_outside = volume_get_at(vol, outside);

    ASSERT(mat_inside == MAT_CONCRETE);
    ASSERT(mat_outside == MAT_AIR);

    volume_destroy(vol);
    return 1;
}

TEST(volume_raycast_determinism)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    Vec3 min_corner = {-2.0f, 0.0f, -2.0f};
    Vec3 max_corner = {2.0f, 4.0f, 2.0f};
    volume_fill_box(vol, min_corner, max_corner, MAT_STONE);

    Vec3 origin = {0.0f, 10.0f, 0.0f};
    Vec3 dir = {0.0f, -1.0f, 0.0f};

    Vec3 hit1, hit2;
    Vec3 normal1, normal2;
    uint8_t mat1, mat2;

    float dist1 = volume_raycast(vol, origin, dir, 20.0f, &hit1, &normal1, &mat1);
    float dist2 = volume_raycast(vol, origin, dir, 20.0f, &hit2, &normal2, &mat2);

    ASSERT(fabsf(dist1 - dist2) < 0.0001f);
    ASSERT(fabsf(hit1.x - hit2.x) < 0.0001f);
    ASSERT(fabsf(hit1.y - hit2.y) < 0.0001f);
    ASSERT(fabsf(hit1.z - hit2.z) < 0.0001f);
    ASSERT(mat1 == mat2);
    ASSERT(mat1 == MAT_STONE);

    volume_destroy(vol);
    return 1;
}

TEST(volume_dirty_tracking)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    volume_begin_frame(vol);

    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    volume_edit_end(vol);

    ASSERT(vol->last_edit_count >= 1);

    volume_destroy(vol);
    return 1;
}

int main(void)
{
    printf("=== Voxel Tests ===\n");

    RUN_TEST(volume_create_destroy);
    RUN_TEST(volume_edit_determinism);
    RUN_TEST(volume_fill_box);
    RUN_TEST(volume_raycast_determinism);
    RUN_TEST(volume_dirty_tracking);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
