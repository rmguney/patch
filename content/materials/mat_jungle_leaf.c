/*
 * mat_jungle_leaf.c - Dense tropical leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_jungle_leaf = {
    .name = "jungle_leaf",
    .r = 25, .g = 100, .b = 25,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.35f,
    .hardness = 0.05f,
    .friction = 0.55f,
    .restitution = 0.02f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .blast_resistance = 0.02f,
    .burn_rate = 0.4f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
