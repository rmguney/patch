/*
 * mat_chrome.c - Highly reflective chrome surface
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_chrome = {
    .name = "chrome",
    .r = 220, .g = 220, .b = 225,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_CONDUCTIVE,
    .density = 7.2f,
    .hardness = 0.8f,
    .friction = 0.3f,
    .restitution = 0.4f,
    .emissive = 0.0f,
    .roughness = 0.05f,
    .blast_resistance = 0.8f,
    .burn_rate = 0.0f,
    .drop_id = MAT_DROP_SELF,
    .tool_tier = TOOL_TIER_IRON,
    .metallic = 1.0f,
};
