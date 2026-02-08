/*
 * mat_birch_bark.c - White birch tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_birch_bark = {
    .name = "birch_bark",
    .r = 200, .g = 195, .b = 175,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.65f,
    .hardness = 0.35f,
    .friction = 0.6f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.25f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
