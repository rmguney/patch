#include "content/materials.h"

const MaterialDescriptor g_mat_snow = {
    .r = 240, .g = 245, .b = 255,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.3f,
    .friction = 0.3f,
    .emissive = 0.0f,
    .roughness = 0.7f,
    .metallic = 0.0f,
};
