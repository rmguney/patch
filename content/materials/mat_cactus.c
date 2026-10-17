/*
 * mat_cactus.c - Desert cactus
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_cactus = {
    .r = 72, .g = 128, .b = 62,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.9f,
    .friction = 0.6f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .metallic = 0.0f,
};
