/*
 * mat_podzol.c - Rust-brown needle litter of the boreal forest floor
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_podzol = {
    .r = 118, .g = 86, .b = 56,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.3f,
    .friction = 0.75f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .metallic = 0.0f,
};
