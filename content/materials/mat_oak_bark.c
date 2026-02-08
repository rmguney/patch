/*
 * mat_oak_bark.c - Oak tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_oak_bark = {
    .name = "oak_bark",
    .r = 90, .g = 45, .b = 15,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.8f,
    .hardness = 0.5f,
    .friction = 0.7f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .blast_resistance = 0.3f,
    .burn_rate = 0.15f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
