#include "content/materials.h"

const MaterialDescriptor g_mat_ice = {
    .name = "ice",
    .r = 180, .g = 220, .b = 245,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_TRANSPARENT,
    .density = 0.9f,
    .hardness = 0.4f,
    .friction = 0.05f,
    .restitution = 0.3f,
    .emissive = 0.0f,
    .roughness = 0.15f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
    .transparency = 0.3f,
    .ior = 1.31f,
};
