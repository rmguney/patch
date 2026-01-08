/*
 * mat_concrete.c - Gray concrete
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_concrete = {
    .name = "concrete",
    .r = 160, .g = 160, .b = 160,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.4f,
    .hardness = 0.7f,
    .friction = 0.65f,
    .restitution = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.8f,
    .blast_resistance = 0.6f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_WOOD,
    .metallic = 0.0f,
};
