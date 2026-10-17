/*
 * mat_pine_leaf.c - Dark pine/conifer needles
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_pine_leaf = {
    .r = 30, .g = 85, .b = 35,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.25f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.88f,
    .metallic = 0.0f,
};
