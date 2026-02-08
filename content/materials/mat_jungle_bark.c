/*
 * mat_jungle_bark.c - Dark tropical tree bark
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_jungle_bark = {
    .name = "jungle_bark",
    .r = 69, .g = 62, .b = 43,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.9f,
    .hardness = 0.55f,
    .friction = 0.75f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.92f,
    .blast_resistance = 0.35f,
    .burn_rate = 0.1f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
