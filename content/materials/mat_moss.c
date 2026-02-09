#include "content/materials.h"

const MaterialDescriptor g_mat_moss = {
    .name = "moss",
    .r = 82, .g = 120, .b = 58,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.4f,
    .hardness = 0.1f,
    .friction = 0.6f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .blast_resistance = 0.05f,
    .burn_rate = 0.3f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
