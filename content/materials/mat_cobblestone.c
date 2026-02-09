#include "content/materials.h"

const MaterialDescriptor g_mat_cobblestone = {
    .name = "cobblestone",
    .r = 115, .g = 110, .b = 105,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.2f,
    .hardness = 0.6f,
    .friction = 0.7f,
    .restitution = 0.15f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .blast_resistance = 0.5f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_WOOD,
    .metallic = 0.0f,
};
