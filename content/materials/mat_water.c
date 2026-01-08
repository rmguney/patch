/*
 * mat_water.c - Blue water (not solid)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_water = {
    .name = "water",
    .r = 64, .g = 164, .b = 223,
    .flags = MAT_FLAG_TRANSPARENT | MAT_FLAG_CONDUCTIVE | MAT_FLAG_LIQUID,
    .density = 1.0f,
    .hardness = 0.0f,
    .friction = 0.05f,
    .restitution = 0.0f,
    .emissive = 0.0f,
    .roughness = 0.05f,
    .blast_resistance = 1.0f,
    .burn_rate = 0.0f,
    .drop_id = MAT_AIR,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
    .transparency = 0.8f,
    .ior = 1.33f,
    .absorption = {0.2f, 0.05f, 0.01f},
};
