#include "scenes.h"

const SceneDescriptor g_scenes[SCENE_MAX_COUNT] = {
    [SCENE_TYPE_BALL_PIT] = {
        .name = "Ball Pit",
        .type = SCENE_TYPE_BALL_PIT,
        .bounds = {.min_x = -25.6f, .max_x = 25.6f, .min_y = 0.0f, .max_y = 19.2f, .min_z = -25.6f, .max_z = 25.6f},
        .chunks_x = 16,
        .chunks_y = 6,
        .chunks_z = 16,
        .voxel_size = 0.1f,
        .rng_seed = 0x12345678,
        .max_entities = 128,
        .max_particles = 8192,
    },
};

const int32_t g_scene_count = SCENE_TYPE_COUNT;

static_assert(SCENE_TYPE_COUNT == 1, "Scene count changed - update g_scenes array");
static_assert(SCENE_TYPE_COUNT <= SCENE_MAX_COUNT, "Too many scene types");
