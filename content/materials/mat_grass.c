/*
 * mat_grass.c - Green grass block
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_grass = {
    .name = "grass",
    .r = 86, .g = 152, .b = 58,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 1.2f,
    .hardness = 0.15f,
    .friction = 0.7f,
    .restitution = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .blast_resistance = 0.1f,
    .burn_rate = 0.3f,
    .drop_id = MAT_DIRT,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
