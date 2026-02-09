#include "content/materials.h"

const MaterialDescriptor g_mat_water = {
    .name = "water",
    .r = 40, .g = 80, .b = 140,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_TRANSPARENT | MAT_FLAG_LIQUID,
    .density = 1.0f,
    .hardness = 0.0f,
    .friction = 0.1f,
    .restitution = 0.0f,
    .emissive = 0.0f,
    .roughness = 0.05f,
    .blast_resistance = 0.0f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
    .transparency = 0.7f,
    .ior = 1.33f,
    .absorption = {0.2f, 0.05f, 0.01f},
};
