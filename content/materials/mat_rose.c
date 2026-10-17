/*
 * mat_rose.c - Decorative rose
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_rose = {
    .r = 242, .g = 204, .b = 217,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
