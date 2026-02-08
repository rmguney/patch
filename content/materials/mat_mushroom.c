/*
 * mat_mushroom.c - Mushroom ground scatter
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_mushroom = {
    .name = "mushroom",
    .r = 180, .g = 140, .b = 100,
    .flags = MAT_FLAG_BREAKABLE,
    .density = 0.15f,
    .hardness = 0.03f,
    .friction = 0.5f,
    .restitution = 0.01f,
    .emissive = 0.0f,
    .roughness = 0.6f,
    .blast_resistance = 0.0f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
