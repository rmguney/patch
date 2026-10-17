/*
 * mat_birch_leaf.c - Light green birch leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_birch_leaf = {
    .r = 100, .g = 160, .b = 40,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.25f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.82f,
    .metallic = 0.0f,
};
