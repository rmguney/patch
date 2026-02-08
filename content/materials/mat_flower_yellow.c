/*
 * mat_flower_yellow.c - Yellow flower ground scatter
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_flower_yellow = {
    .name = "flower_yellow",
    .r = 220, .g = 200, .b = 40,
    .flags = MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.1f,
    .hardness = 0.02f,
    .friction = 0.4f,
    .restitution = 0.01f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .blast_resistance = 0.0f,
    .burn_rate = 0.8f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
