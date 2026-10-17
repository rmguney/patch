/*
 * mat_jungle_bark.c - Dark tropical tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_jungle_bark = {
    .r = 69, .g = 62, .b = 43,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.9f,
    .friction = 0.75f,
    .emissive = 0.0f,
    .roughness = 0.92f,
    .metallic = 0.0f,
};
