#include "content/materials.h"

const MaterialDescriptor g_mat_glowing_rock = {
    .name = "glowing_rock",
    .r = 100, .g = 180, .b = 200,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.4f,
    .hardness = 0.5f,
    .friction = 0.5f,
    .restitution = 0.1f,
    .emissive = 1.5f,
    .roughness = 0.3f,
    .blast_resistance = 0.4f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_WOOD,
    .metallic = 0.1f,
};
