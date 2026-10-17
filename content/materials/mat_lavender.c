/*
 * mat_lavender.c - Decorative lavender
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_lavender = {
    .r = 242, .g = 191, .b = 204,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
