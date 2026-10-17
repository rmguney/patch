/*
 * mat_oak_bark.c - Oak tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_oak_bark = {
    .r = 90, .g = 45, .b = 15,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.8f,
    .friction = 0.7f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .metallic = 0.0f,
};
