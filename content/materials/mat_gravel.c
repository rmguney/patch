#include "content/materials.h"

const MaterialDescriptor g_mat_gravel = {
    .r = 140, .g = 135, .b = 125,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.7f,
    .friction = 0.75f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .metallic = 0.0f,
};
