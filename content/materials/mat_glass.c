/*
 * mat_glass.c - Transparent glass material
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_glass = {
    .name = "glass",
    .r = 220, .g = 230, .b = 240,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_TRANSPARENT,
    .density = 2.5f,
    .hardness = 0.2f,
    .friction = 0.3f,
    .restitution = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.05f,
    .blast_resistance = 0.1f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
    .transparency = 0.7f,
    .ior = 1.5f,
};
