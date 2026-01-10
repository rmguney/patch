/*
 * shape_sphere.c - 5x5x5 sphere (approximated hollow shell)
 *
 * Hand-authored primitive shape.
 */
#include "content/voxel_shapes.h"
#include "content/materials.h"

static const uint8_t k_sphere_voxels[5 * 5 * 5] = {
    /* z=0 */
    0, 0, 0, 0, 0,
    0, 0, MAT_METAL, 0, 0,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    0, 0, MAT_METAL, 0, 0,
    0, 0, 0, 0, 0,
    /* z=1 */
    0, 0, MAT_METAL, 0, 0,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    0, 0, MAT_METAL, 0, 0,
    /* z=2 */
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL,
    MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL,
    MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    /* z=3 */
    0, 0, MAT_METAL, 0, 0,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL, MAT_METAL,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    0, 0, MAT_METAL, 0, 0,
    /* z=4 */
    0, 0, 0, 0, 0,
    0, 0, MAT_METAL, 0, 0,
    0, MAT_METAL, MAT_METAL, MAT_METAL, 0,
    0, 0, MAT_METAL, 0, 0,
    0, 0, 0, 0, 0,
};

const VoxelShape g_shape_sphere = {
    .name = "sphere",
    .size_x = 5,
    .size_y = 5,
    .size_z = 5,
    .voxels = k_sphere_voxels,
    .solid_count = 57,
};
