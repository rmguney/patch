/*
 * mat_sky.c - Decorative sky (emissive)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_sky = {
    .name = "sky",
    .r = 191, .g = 204, .b = 242,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.0f,
    .hardness = 0.3f,
    .friction = 0.5f,
    .restitution = 0.3f,
    .emissive = 0.25f,
    .roughness = 0.5f,
    .blast_resistance = 0.2f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
