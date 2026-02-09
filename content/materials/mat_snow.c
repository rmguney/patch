#include "content/materials.h"

const MaterialDescriptor g_mat_snow = {
    .name = "snow",
    .r = 240, .g = 245, .b = 255,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.3f,
    .hardness = 0.1f,
    .friction = 0.3f,
    .restitution = 0.0f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .blast_resistance = 0.05f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
