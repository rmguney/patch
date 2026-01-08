/*
 * mat_glass.c - Transparent glass
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_glass = {
    .name = "glass",
    .r = 200, .g = 220, .b = 255,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_TRANSPARENT,
    .density = 2.5f,
    .hardness = 0.5f,
    .friction = 0.3f,
    .restitution = 0.2f,
    .emissive = 0.0f,
    .roughness = 0.1f,
    .blast_resistance = 0.05f,
    .burn_rate = 0.0f,
    .drop_id = MAT_AIR,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
    .transparency = 0.9f,
    .ior = 1.5f,
    .absorption = {0.0f, 0.0f, 0.0f},
};
