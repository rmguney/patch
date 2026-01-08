/*
 * mat_dirt.c - Brown dirt/soil
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_dirt = {
    .name = "dirt",
    .r = 139, .g = 90, .b = 43,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.5f,
    .hardness = 0.2f,
    .friction = 0.8f,
    .restitution = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .blast_resistance = 0.1f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_HAND,
    .metallic = 0.0f,
};
