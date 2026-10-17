/*
 * mat_metal.c - Gray metal (unbreakable)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_metal = {
    .r = 168, .g = 155, .b = 140,
    .flags = MAT_FLAG_SOLID,
    .density = 7.8f,
    .friction = 0.4f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 1.0f,
};
