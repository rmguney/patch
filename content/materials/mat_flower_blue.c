/*
 * mat_flower_blue.c - Blue flower ground scatter
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_flower_blue = {
    .r = 60, .g = 80, .b = 200,
    .flags = MAT_FLAG_BREAKABLE,
    .density = 0.1f,
    .friction = 0.4f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .metallic = 0.0f,
};
