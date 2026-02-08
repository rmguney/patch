/*
 * mat_pine_bark.c - Pine/conifer tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_pine_bark = {
    .name = "pine_bark",
    .r = 79, .g = 102, .b = 105,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.7f,
    .hardness = 0.4f,
    .friction = 0.7f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .blast_resistance = 0.25f,
    .burn_rate = 0.2f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
