/*
 * mat_pink.c - Decorative pink
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_pink = {
    .r = 255,
    .g = 197,
    .b = 211,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
