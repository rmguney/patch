/*
 * mat_rose.c - Decorative rose
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_rose = {
    .name = "rose",
    .r = 242, .g = 204, .b = 217,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .hardness = 0.3f,
    .friction = 0.5f,
    .restitution = 0.3f,
    .emissive = 0.0f,
    .roughness = 0.6f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
