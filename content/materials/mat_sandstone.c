/*
 * mat_sandstone.c - Orange desert bedrock (desert cliffs and the layer under the dunes)
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_sandstone = {
    .r = 198, .g = 140, .b = 88,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 2.2f,
    .friction = 0.75f,
    .emissive = 0.0f,
    .roughness = 0.85f,
    .metallic = 0.0f,
};
