/*
 * mat_cherry_leaf.c - Pink cherry blossom leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_cherry_leaf = {
    .name = "cherry_leaf",
    .r = 200, .g = 100, .b = 120,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.2f,
    .hardness = 0.04f,
    .friction = 0.45f,
    .restitution = 0.02f,
    .emissive = 0.0f,
    .roughness = 0.75f,
    .blast_resistance = 0.01f,
    .burn_rate = 0.55f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
