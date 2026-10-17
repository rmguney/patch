/*
 * mat_autumn_leaf.c - Orange/red autumn leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_autumn_leaf = {
    .r = 180, .g = 80, .b = 20,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.2f,
    .friction = 0.45f,
    .emissive = 0.0f,
    .roughness = 0.82f,
    .metallic = 0.0f,
};
