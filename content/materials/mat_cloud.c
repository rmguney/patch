/*
 * mat_cloud.c - Decorative cloud
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_cloud = {
    .r = 217, .g = 230, .b = 242,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
