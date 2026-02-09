#include "scenes.h"

const SceneDescriptor g_scenes[SCENE_MAX_COUNT] = {
    [SCENE_TYPE_BALL_PIT] = {
        .name = "Ball Pit",
        .type = SCENE_TYPE_BALL_PIT,
        .bounds = {.min_x = -51.2f, .max_x = 51.2f, .min_y = 0.0f, .max_y = 32.0f, .min_z = -51.2f, .max_z = 51.2f},
        .chunks_x = 32,
        .chunks_y = 10,
        .chunks_z = 32,
        .voxel_size = 0.1f,
        .rng_seed = 0x12345678,
        .max_entities = 256,
        .max_particles = 16384,
        .lighting = SCENE_LIGHTING_DEFAULT,
    },
};

const int32_t g_scene_count = SCENE_TYPE_COUNT;

STATIC_ASSERT(SCENE_TYPE_COUNT == 1, "Scene count changed - update g_scenes array");
STATIC_ASSERT(SCENE_TYPE_COUNT <= SCENE_MAX_COUNT, "Too many scene types");
