#include "scenes.h"

const SceneDescriptor g_scenes[SCENE_TYPE_COUNT] = {
    [SCENE_TYPE_WORLD] = {
        .name = "World",
        .bounds = {.min_x = -76.8f, .max_x = 76.8f, .min_y = 0.0f, .max_y = 38.4f, .min_z = -76.8f, .max_z = 76.8f},
        .chunks_x = 48,
        .chunks_y = 12,
        .chunks_z = 48,
        .voxel_size = 0.1f,
        .rng_seed = 0x12345678,
        .lighting = SCENE_LIGHTING_DEFAULT,
    },
};
