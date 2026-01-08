/*
 * mat_air.c - Empty space material (ID 0)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_air = {
    .name = "air",
    .r = 0, .g = 0, .b = 0,
    .flags = MAT_FLAG_TRANSPARENT,
    .density = 0.0f,
    .hardness = 0.0f,
    .friction = 0.0f,
    .restitution = 0.0f,
    .emissive = 0.0f,
    .roughness = 1.0f,
    .blast_resistance = 0.0f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
