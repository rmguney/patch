/*
 * mat_mint.c - Decorative mint
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_mint = {
    .r = 218,
    .g = 255,
    .b = 231,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
