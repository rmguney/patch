#include "content/materials.h"

const MaterialDescriptor g_mat_lava = {
    .name = "lava",
    .r = 200, .g = 70, .b = 20,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_LIQUID,
    .density = 1.5f,
    .hardness = 0.0f,
    .friction = 0.1f,
    .restitution = 0.0f,
    .emissive = 2.5f,
    .roughness = 0.9f,
    .blast_resistance = 1.0f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
    .transparency = 0.0f,
    .ior = 1.0f,
    .absorption = {0.0f, 0.0f, 0.0f},
};
