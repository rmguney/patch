/*
 * mat_mud.c - Dark waterlogged swamp soil
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_mud = {
    .r = 82, .g = 70, .b = 48,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.7f,
    .friction = 0.5f,
    .emissive = 0.0f,
    .roughness = 0.6f,
    .metallic = 0.0f,
};
