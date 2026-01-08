/*
 * mat_wood.c - Brown wood
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_wood = {
    .name = "wood",
    .r = 139, .g = 90, .b = 43,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.7f,
    .hardness = 0.4f,
    .friction = 0.5f,
    .restitution = 0.2f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.5f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
