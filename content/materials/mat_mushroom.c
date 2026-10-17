/*
 * mat_mushroom.c - Mushroom ground scatter
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_mushroom = {
    .r = 180, .g = 140, .b = 100,
    .flags = MAT_FLAG_BREAKABLE,
    .density = 0.15f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.6f,
    .metallic = 0.0f,
};
