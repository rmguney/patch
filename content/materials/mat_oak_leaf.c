/*
 * mat_oak_leaf.c - Green deciduous oak leaves
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_oak_leaf = {
    .name = "oak_leaf",
    .r = 50, .g = 120, .b = 30,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.3f,
    .hardness = 0.05f,
    .friction = 0.5f,
    .restitution = 0.02f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .blast_resistance = 0.02f,
    .burn_rate = 0.5f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
