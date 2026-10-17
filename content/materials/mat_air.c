/*
 * mat_air.c - Empty space material (ID 0)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_air = {
    .r = 0, .g = 0, .b = 0,
    .flags = MAT_FLAG_TRANSPARENT,
    .density = 0.0f,
    .friction = 0.0f,
    .emissive = 0.0f,
    .roughness = 1.0f,
    .metallic = 0.0f,
};
