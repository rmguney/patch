/*
 * mat_orange.c - Orange color for voxel objects
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_orange = {
    .r = 255, .g = 140, .b = 0,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .metallic = 0.0f,
};
