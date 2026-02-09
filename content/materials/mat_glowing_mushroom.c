#include "content/materials.h"

const MaterialDescriptor g_mat_glowing_mushroom = {
    .name = "glowing_mushroom",
    .r = 140, .g = 220, .b = 160,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.3f,
    .hardness = 0.1f,
    .friction = 0.4f,
    .restitution = 0.05f,
    .emissive = 2.0f,
    .roughness = 0.4f,
    .blast_resistance = 0.05f,
    .burn_rate = 0.4f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
