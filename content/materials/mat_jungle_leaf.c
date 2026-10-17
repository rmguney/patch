/*
 * mat_jungle_leaf.c - Dense tropical leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_jungle_leaf = {
    .r = 25, .g = 100, .b = 25,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.35f,
    .friction = 0.55f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .metallic = 0.0f,
};
