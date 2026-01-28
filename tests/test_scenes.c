#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/sim/scene.h"
#include "engine/platform/platform.h"
#include "game/ball_pit.h"
#include "content/scenes.h"
#include "test_common.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

#define FRAME_BUDGET_MS 16.667f

TEST(ball_pit_create_destroy)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    ASSERT(desc != NULL);

    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);
    ASSERT(scene->user_data != NULL);

    BallPitData *data = (BallPitData *)scene->user_data;
    ASSERT(data->spawn_timer > 0.0f);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_init)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);

    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    BallPitData *data = (BallPitData *)scene->user_data;
    ASSERT(data->stats.spawn_count == 0);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_tick_increments_stats)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, NULL);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    BallPitData *data = (BallPitData *)scene->user_data;
    int32_t initial_tick_count = data->stats.tick_count;

    scene_update(scene, 1.0f / 60.0f);

    ASSERT(data->stats.tick_count > initial_tick_count);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_spawn_timer_works)
{
    BallPitParams params = ball_pit_default_params();
    params.spawn_interval = 0.1f;
    params.spawn_batch = 5;
    params.max_spawns = 100;

    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, &params);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    BallPitData *data = (BallPitData *)scene->user_data;
    int32_t initial_spawns = data->stats.spawn_count;

    for (int i = 0; i < 10; i++)
    {
        scene_update(scene, 0.02f);
    }

    ASSERT(data->stats.spawn_count > initial_spawns);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_custom_params)
{
    BallPitParams params = ball_pit_default_params();
    params.initial_spawns = 10;
    params.spawn_interval = 2.0f;
    params.spawn_batch = 3;
    params.max_spawns = 50;

    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, &params);
    ASSERT(scene != NULL);

    BallPitData *data = (BallPitData *)scene->user_data;
    ASSERT(data->params.initial_spawns == 10);
    ASSERT(data->params.spawn_interval == 2.0f);
    ASSERT(data->params.spawn_batch == 3);
    ASSERT(data->params.max_spawns == 50);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_stress_env_override)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, NULL);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_ray_setting)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, NULL);

    Vec3 origin = vec3_create(1.0f, 2.0f, 3.0f);
    Vec3 dir = vec3_create(0.0f, -1.0f, 0.0f);
    ball_pit_set_ray(scene, origin, dir);

    BallPitData *data = (BallPitData *)scene->user_data;
    ASSERT_NEAR(data->ray_origin.x, 1.0f, 0.001f);
    ASSERT_NEAR(data->ray_origin.y, 2.0f, 0.001f);
    ASSERT_NEAR(data->ray_origin.z, 3.0f, 0.001f);
    ASSERT_NEAR(data->ray_dir.y, -1.0f, 0.001f);

    scene_destroy(scene);
    return 1;
}

TEST(ball_pit_performance)
{
    platform_time_init();

    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
    Scene *scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, NULL);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    for (int32_t i = 0; i < 10; i++)
        scene_update(scene, 1.0f / 60.0f);

    float total_ms = 0.0f;
    float max_ms = 0.0f;
    const int32_t FRAMES = 60;

    for (int32_t i = 0; i < FRAMES; i++)
    {
        PlatformTime t0 = platform_time_now();
        scene_update(scene, 1.0f / 60.0f);
        PlatformTime t1 = platform_time_now();

        float ms = (float)(platform_time_delta_seconds(t0, t1) * 1000.0);
        total_ms += ms;
        if (ms > max_ms)
            max_ms = ms;
    }

    float avg_ms = total_ms / FRAMES;
    float budget_pct = (avg_ms / FRAME_BUDGET_MS) * 100.0f;

    printf("\n    Blank scene tick: avg=%.2fms max=%.2fms budget=%.1f%%", avg_ms, max_ms, budget_pct);

    ASSERT(avg_ms < FRAME_BUDGET_MS * 0.1f);

    scene_destroy(scene);
    return 1;
}

int main(void)
{
    printf("=== Scene Tests ===\n");

    RUN_TEST(ball_pit_create_destroy);
    RUN_TEST(ball_pit_init);
    RUN_TEST(ball_pit_tick_increments_stats);
    RUN_TEST(ball_pit_spawn_timer_works);
    RUN_TEST(ball_pit_custom_params);
    RUN_TEST(ball_pit_stress_env_override);
    RUN_TEST(ball_pit_ray_setting);
    RUN_TEST(ball_pit_performance);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
