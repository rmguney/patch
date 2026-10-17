/*
 * mat_dead_wood.c - Dead/dry tree wood
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_dead_wood = {
    .r = 55, .g = 34, .b = 32,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.5f,
    .friction = 0.65f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .metallic = 0.0f,
};
