/*
 * shape_axe.c - 3x8x1 axe (handle + head)
 *
 * Hand-authored primitive shape.
 */
#include "content/voxel_shapes.h"
#include "content/materials.h"

static const uint8_t k_axe_voxels[3 * 8 * 1] = {
    /* Handle bottom */
    0, MAT_WOOD, 0,         /* y=0 */
    0, MAT_WOOD, 0,         /* y=1 */
    0, MAT_WOOD, 0,         /* y=2 */
    0, MAT_WOOD, 0,         /* y=3 */
    0, MAT_WOOD, 0,         /* y=4 */
    /* Axe head */
    MAT_METAL, MAT_WOOD, 0, /* y=5 */
    MAT_METAL, MAT_METAL, 0,/* y=6 */
    MAT_METAL, MAT_METAL, 0,/* y=7 top */
};

const VoxelShape g_shape_axe = {
    .name = "axe",
    .size_x = 3,
    .size_y = 8,
    .size_z = 1,
    .voxels = k_axe_voxels,
    .solid_count = 13,
};
