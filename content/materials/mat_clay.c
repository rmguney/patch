#include "content/materials.h"

const MaterialDescriptor g_mat_clay = {
    .name = "clay",
    .r = 180, .g = 140, .b = 100,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.8f,
    .hardness = 0.3f,
    .friction = 0.65f,
    .restitution = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
