/*
 * mat_grass.c - Green grass block
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_grass = {
    .r = 86, .g = 152, .b = 58,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.2f,
    .friction = 0.7f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .metallic = 0.0f,
};
