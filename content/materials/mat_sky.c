/*
 * mat_sky.c - Decorative sky
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_sky = {
    .r = 191, .g = 204, .b = 242,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
