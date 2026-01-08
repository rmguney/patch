#include "game/ball_pit.h"
#include "engine/platform/platform.h"
#include "engine/core/profile.h"
#include "engine/core/math.h"
#include <stdlib.h>
#include <string.h>

static void ball_pit_init(Scene *scene)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    const BallPitParams *p = &data->params;

    int32_t spawn_target = p->initial_spawns;
    const char *stress_env = getenv("PATCH_STRESS_OBJECTS");
    if (stress_env)
    {
        int32_t stress_count = atoi(stress_env);
        if (stress_count > 0 && stress_count <= p->max_spawns)
            spawn_target = stress_count;
    }

    data->stats.spawn_count = spawn_target;
    data->spawn_timer = p->spawn_interval;
}

static void ball_pit_destroy_impl(Scene *scene)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    free(data);
    free(scene);
}

static void ball_pit_tick(Scene *scene)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    const float dt = SIM_TIMESTEP;

    PROFILE_BEGIN(PROFILE_SIM_TICK);

    PlatformTime t0 = platform_time_now();

    data->stats.tick_count++;

    const BallPitParams *p = &data->params;
    data->spawn_timer -= dt;
    if (data->spawn_timer <= 0.0f && data->stats.spawn_count < p->max_spawns)
    {
        PROFILE_BEGIN(PROFILE_PROP_SPAWN);
        data->stats.spawn_count += p->spawn_batch;
        data->spawn_timer = p->spawn_interval;
        PROFILE_END(PROFILE_PROP_SPAWN);
    }

    PlatformTime t1 = platform_time_now();
    data->stats.tick_time_us = platform_time_delta_seconds(t0, t1) * 1000000.0f;

    PROFILE_END(PROFILE_SIM_TICK);
}

static void ball_pit_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    (void)scene;
    (void)mouse_x;
    (void)mouse_y;
    (void)left_down;
    (void)right_down;
}

static const char *ball_pit_get_name(Scene *scene)
{
    (void)scene;
    return "Blank Scene";
}

static const SceneVTable ball_pit_vtable = {
    .init = ball_pit_init,
    .destroy = ball_pit_destroy_impl,
    .tick = ball_pit_tick,
    .handle_input = ball_pit_handle_input,
    .get_name = ball_pit_get_name};

BallPitParams ball_pit_default_params(void)
{
    BallPitParams p;
    p.initial_spawns = 0;
    p.spawn_interval = 1.0f;
    p.spawn_batch = 1;
    p.max_spawns = 1024;
    return p;
}

Scene *ball_pit_scene_create(Bounds3D bounds, float voxel_size, const BallPitParams *params)
{
    Scene *scene = (Scene *)calloc(1, sizeof(Scene));
    if (!scene)
        return NULL;

    BallPitData *data = (BallPitData *)calloc(1, sizeof(BallPitData));
    if (!data)
    {
        free(scene);
        return NULL;
    }

    data->params = params ? *params : ball_pit_default_params();
    data->spawn_timer = data->params.spawn_interval;
    data->voxel_size = voxel_size;
    data->ray_origin = vec3_zero();
    data->ray_dir = vec3_create(0.0f, 0.0f, -1.0f);

    scene->vtable = &ball_pit_vtable;
    scene->bounds = bounds;
    scene->user_data = data;
    rng_seed(&scene->rng, 12345);

    return scene;
}

void ball_pit_scene_destroy(Scene *scene)
{
    scene_destroy(scene);
}

void ball_pit_set_ray(Scene *scene, Vec3 origin, Vec3 dir)
{
    if (!scene || !scene->user_data)
        return;
    BallPitData *data = (BallPitData *)scene->user_data;
    data->ray_origin = origin;
    data->ray_dir = dir;
}
