/*
 * mat_birch_bark.c - White birch tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_birch_bark = {
    .r = 200, .g = 195, .b = 175,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.65f,
    .friction = 0.6f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .metallic = 0.0f,
};
