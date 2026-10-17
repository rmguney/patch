/*
 * mat_ice.c - Lake ice of the snow biome
 *
 * Opaque on purpose: the raymarcher renders every transparent voxel as water, so a
 * transparent ice sheet would look like open water.
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_ice = {
    .r = 186, .g = 220, .b = 240,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.9f,
    .friction = 0.05f,
    .emissive = 0.0f,
    .roughness = 0.1f,
    .metallic = 0.0f,
};
