/*
 * mat_dry_grass.c - Sun-bleached savannah grass (also the reed stalks of swamps)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_dry_grass = {
    .r = 196, .g = 170, .b = 88,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.1f,
    .friction = 0.7f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .metallic = 0.0f,
};
