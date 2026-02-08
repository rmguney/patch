/*
 * mat_dead_wood.c - Dead/dry tree wood
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_dead_wood = {
    .name = "dead_wood",
    .r = 55, .g = 34, .b = 32,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE | MAT_FLAG_FLAMMABLE,
    .density = 0.5f,
    .hardness = 0.3f,
    .friction = 0.65f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .blast_resistance = 0.15f,
    .burn_rate = 0.4f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
