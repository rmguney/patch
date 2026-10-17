/*
 * mat_water.c - Clear lake and sea water
 *
 * A transparent material's colour is what white light keeps of itself after 10 m of it
 * (materials.h): this one absorbs 0.2 / m of red, 0.05 / m of green and 0.01 / m of blue.
 */
#include "content/materials.h"

const MaterialDescriptor g_mat_water = {
    .r = 103, .g = 204, .b = 244,
    .flags = MAT_FLAG_TRANSPARENT | MAT_FLAG_LIQUID,
    .density = 1.0f,
    .friction = 0.1f,
    .emissive = 0.0f,
    .roughness = 0.05f,
    .metallic = 0.0f,
};
