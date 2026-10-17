/*
 * mat_flower_red.c - Red flower ground scatter
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_flower_red = {
    .r = 200, .g = 50, .b = 50,
    .flags = MAT_FLAG_BREAKABLE,
    .density = 0.1f,
    .friction = 0.4f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .metallic = 0.0f,
};
