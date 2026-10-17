/*
 * mat_flower_yellow.c - Yellow flower ground scatter
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_flower_yellow = {
    .r = 220, .g = 200, .b = 40,
    .flags = MAT_FLAG_BREAKABLE,
    .density = 0.1f,
    .friction = 0.4f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .metallic = 0.0f,
};
