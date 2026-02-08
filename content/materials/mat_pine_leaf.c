/*
 * mat_pine_leaf.c - Dark pine/conifer needles
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_pine_leaf = {
    .name = "pine_leaf",
    .r = 30, .g = 85, .b = 35,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.25f,
    .hardness = 0.05f,
    .friction = 0.5f,
    .restitution = 0.02f,
    .emissive = 0.0f,
    .roughness = 0.88f,
    .blast_resistance = 0.02f,
    .burn_rate = 0.6f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
