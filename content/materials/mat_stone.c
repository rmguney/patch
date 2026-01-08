/*
 * mat_stone.c - Generic gray stone
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_stone = {
    .name = "stone",
    .r = 128, .g = 128, .b = 128,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.5f,
    .hardness = 0.8f,
    .friction = 0.6f,
    .restitution = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .blast_resistance = 0.6f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_WOOD,
    .metallic = 0.0f,
};
