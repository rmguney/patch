/*
 * mat_jungle_grass.c - Saturated blue-green rainforest ground cover
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_jungle_grass = {
    .r = 38, .g = 132, .b = 70,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.2f,
    .friction = 0.7f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .metallic = 0.0f,
};
