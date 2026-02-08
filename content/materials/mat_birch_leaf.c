/*
 * mat_birch_leaf.c - Light green birch leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_birch_leaf = {
    .name = "birch_leaf",
    .r = 100, .g = 160, .b = 40,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.25f,
    .hardness = 0.05f,
    .friction = 0.5f,
    .restitution = 0.02f,
    .emissive = 0.0f,
    .roughness = 0.82f,
    .blast_resistance = 0.02f,
    .burn_rate = 0.45f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
