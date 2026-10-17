/*
 * mat_coral.c - Decorative coral
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_coral = {
    .r = 230, .g = 179, .b = 191,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
