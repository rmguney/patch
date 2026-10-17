/*
 * mat_swamp_water.c - Murky, tea-brown standing water of the swamp biome
 *
 * A transparent material's colour is what white light keeps of itself after 10 m of it
 * (materials.h): per metre it lets through about three quarters of the green, two thirds of
 * the red and half of the blue.
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_swamp_water = {
    .r = 37, .g = 63, .b = 3,
    .flags = MAT_FLAG_TRANSPARENT | MAT_FLAG_LIQUID,
    .density = 1.0f,
    .friction = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.05f,
    .metallic = 0.0f,
};
