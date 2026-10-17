#include "content/materials.h"

const MaterialDescriptor g_mat_moss = {
    .r = 82, .g = 120, .b = 58,
    .flags = MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE,
    .density = 0.4f,
    .friction = 0.6f,
    .emissive = 0.0f,
    .roughness = 0.9f,
    .metallic = 0.0f,
};
