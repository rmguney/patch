#include "content/materials.h"

const MaterialDescriptor g_mat_glowing_mushroom = {
    .r = 140, .g = 220, .b = 160,
    .flags = MAT_FLAG_BREAKABLE,
    .density = 0.3f,
    .friction = 0.4f,
    .emissive = 2.0f,
    .roughness = 0.4f,
    .metallic = 0.0f,
};
