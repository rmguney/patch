/*
 * mat_stone.c - Generic gray stone
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_stone = {
    .r = 128, .g = 128, .b = 128,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.5f,
    .friction = 0.6f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .metallic = 0.0f,
};
