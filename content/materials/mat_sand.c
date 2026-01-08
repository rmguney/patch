/*
 * mat_sand.c - Tan sand
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_sand = {
    .name = "sand",
    .r = 214, .g = 186, .b = 140,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.6f,
    .hardness = 0.1f,
    .friction = 0.5f,
    .restitution = 0.02f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .blast_resistance = 0.1f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
