/*
 * mat_brick.c - Red brick
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_brick = {
    .name = "brick",
    .r = 178, .g = 84, .b = 58,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.8f,
    .hardness = 0.6f,
    .friction = 0.7f,
    .restitution = 0.15f,
    .emissive = 0.0f,
    .roughness = 0.75f,
    .blast_resistance = 0.5f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_WOOD,
    .metallic = 0.0f,
};
