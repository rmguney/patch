/*
 * mat_glow.c - Bright emissive material
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_glow = {
    .name = "glow",
    .r = 255, .g = 140, .b = 0,
    .flags = MAT_FLAG_SOLID,
    .density = 1.0f,
    .hardness = 0.3f,
    .friction = 0.5f,
    .restitution = 0.2f,
    .emissive = 2.0f,
    .roughness = 0.8f,
    .blast_resistance = 0.1f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
