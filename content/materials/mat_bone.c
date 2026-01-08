/*
 * mat_bone.c - White bone
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_bone = {
    .name = "bone",
    .r = 240, .g = 234, .b = 214,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.9f,
    .hardness = 0.6f,
    .friction = 0.4f,
    .restitution = 0.2f,
    .emissive = 0.0f,
    .roughness = 0.5f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
