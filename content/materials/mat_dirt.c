/*
 * mat_dirt.c - Brown dirt/soil
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_dirt = {
    .r = 139, .g = 90, .b = 43,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.5f,
    .friction = 0.8f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .metallic = 0.0f,
};
