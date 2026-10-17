/*
 * mat_cyan.c - Decorative cyan
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_cyan = {
    .r = 140, .g = 217, .b = 217,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
