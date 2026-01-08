/*
 * mat_flesh.c - Organic flesh
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_flesh = {
    .name = "flesh",
    .r = 217, .g = 115, .b = 115,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 1.0f,
    .hardness = 0.1f,
    .friction = 0.6f,
    .restitution = 0.3f,
    .emissive = 0.0f,
    .roughness = 0.6f,
    .blast_resistance = 0.0f,
    .burn_rate = 0.4f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
