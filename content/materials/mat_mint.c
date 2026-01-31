/*
 * mat_mint.c - Decorative mint
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_mint = {
    .name = "mint",
    .r = 218,
    .g = 255,
    .b = 231,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .hardness = 0.3f,
    .friction = 0.5f,
    .restitution = 0.3f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
