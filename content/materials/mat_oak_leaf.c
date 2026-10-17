/*
 * mat_oak_leaf.c - Green deciduous oak leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_oak_leaf = {
    .r = 50, .g = 120, .b = 30,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.3f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .metallic = 0.0f,
};
