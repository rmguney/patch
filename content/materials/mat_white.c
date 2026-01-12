/*
 * mat_white.c - Pure white decorative material
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_white = {
    .name = "white",
    .r = 255, .g = 255, .b = 255,
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
