/*
 * shape_cube.c - 4x4x4 solid cube
 *
 * Hand-authored primitive shape.
 */
#include "content/voxel_shapes.h"
#include "content/materials.h"

static const uint8_t k_cube_voxels[4 * 4 * 4] = {
    /* z=0 */
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    /* z=1 */
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    /* z=2 */
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    /* z=3 */
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
    MAT_STONE, MAT_STONE, MAT_STONE, MAT_STONE,
};

const VoxelShape g_shape_cube = {
    .name = "cube",
    .size_x = 4,
    .size_y = 4,
    .size_z = 4,
    .voxels = k_cube_voxels,
    .solid_count = 64,
};
