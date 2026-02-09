#include "content/materials.h"

const MaterialDescriptor g_mat_weak_rock = {
    .name = "weak_rock",
    .r = 155, .g = 145, .b = 130,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.0f,
    .hardness = 0.3f,
    .friction = 0.6f,
    .restitution = 0.15f,
    .emissive = 0.0f,
    .roughness = 0.75f,
    .blast_resistance = 0.25f,
    .burn_rate = 0.0f,
    .drop_id = MAT_STONE,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
