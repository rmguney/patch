/*
 * mat_cherry_leaf.c - Pink cherry blossom leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_cherry_leaf = {
    .r = 200, .g = 100, .b = 120,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.2f,
    .friction = 0.45f,
    .emissive = 0.0f,
    .roughness = 0.75f,
    .metallic = 0.0f,
};
