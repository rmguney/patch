#include "content/materials.h"

const MaterialDescriptor g_mat_sand = {
    .name = "sand",
    .r = 214, .g = 199, .b = 164,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.6f,
    .hardness = 0.2f,
    .friction = 0.7f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .blast_resistance = 0.1f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
