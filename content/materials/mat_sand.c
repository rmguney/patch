#include "content/materials.h"

const MaterialDescriptor g_mat_sand = {
    .r = 214, .g = 199, .b = 164,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 1.6f,
    .friction = 0.7f,
    .emissive = 0.0f,
    .roughness = 0.95f,
    .metallic = 0.0f,
};
