/*
 * mat_pine_bark.c - Pine/conifer tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_pine_bark = {
    .r = 79, .g = 102, .b = 105,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.7f,
    .friction = 0.7f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .metallic = 0.0f,
};
