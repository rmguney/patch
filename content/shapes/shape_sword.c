/*
 * shape_sword.c - 2x12x1 sword (blade + handle)
 *
 * Hand-authored primitive shape.
 */
#include "content/voxel_shapes.h"
#include "content/materials.h"

static const uint8_t k_sword_voxels[2 * 12 * 1] = {
    /* Blade (metal) from tip to hilt */
    MAT_METAL, MAT_METAL,   /* y=0 tip */
    MAT_METAL, MAT_METAL,   /* y=1 */
    MAT_METAL, MAT_METAL,   /* y=2 */
    MAT_METAL, MAT_METAL,   /* y=3 */
    MAT_METAL, MAT_METAL,   /* y=4 */
    MAT_METAL, MAT_METAL,   /* y=5 */
    MAT_METAL, MAT_METAL,   /* y=6 */
    MAT_METAL, MAT_METAL,   /* y=7 blade end */
    MAT_WOOD, MAT_WOOD,     /* y=8 guard */
    MAT_WOOD, MAT_WOOD,     /* y=9 handle */
    MAT_WOOD, MAT_WOOD,     /* y=10 handle */
    MAT_WOOD, MAT_WOOD,     /* y=11 pommel */
};

const VoxelShape g_shape_sword = {
    .name = "sword",
    .size_x = 2,
    .size_y = 12,
    .size_z = 1,
    .voxels = k_sword_voxels,
    .solid_count = 24,
    .center_of_mass_x = 1.0f,
    .center_of_mass_y = 6.0f,
    .center_of_mass_z = 0.5f,
};
