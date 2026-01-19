/*
 * mat_red.c - Pastel red material
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_red = {
    .name = "red",
    .r = 238,
    .g = 105,
    .b = 105,
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
