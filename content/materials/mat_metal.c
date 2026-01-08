/*
 * mat_metal.c - Gray metal (high tier required to break)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_metal = {
    .name = "metal",
    .r = 192, .g = 192, .b = 200,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_CONDUCTIVE,
    .density = 7.8f,
    .hardness = 0.95f,
    .friction = 0.4f,
    .restitution = 0.3f,
    .emissive = 0.0f,
    .roughness = 0.25f,
    .blast_resistance = 0.9f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_DIAMOND,
    .metallic = 1.0f,
};
