#include "content/materials.h"

const MaterialDescriptor g_mat_gravel = {
    .name = "gravel",
    .r = 140, .g = 135, .b = 125,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.7f,
    .hardness = 0.25f,
    .friction = 0.75f,
    .restitution = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .blast_resistance = 0.15f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
