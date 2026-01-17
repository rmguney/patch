/*
 * mat_green.c - Pastel green material for GI color bleeding tests
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_green = {
    .name = "green",
    .r = 147,
    .g = 197,
    .b = 114,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.0f,
    .hardness = 0.5f,
    .friction = 0.6f,
    .restitution = 0.2f,
    .emissive = 0.0f,
    .roughness = 0.6f,
    .blast_resistance = 0.3f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
