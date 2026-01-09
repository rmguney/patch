#include "game/ball_pit.h"
#include "engine/platform/platform.h"
#include "engine/core/profile.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/physics/voxel_body.h"
#include "content/voxel_shapes.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Pastel material palette for spawned objects */
static const uint8_t s_pastel_materials[] = {
    MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
    MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE};
static const int32_t s_pastel_count = sizeof(s_pastel_materials) / sizeof(s_pastel_materials[0]);

static uint8_t pick_pastel_material(RngState *rng)
{
    return s_pastel_materials[rng_range_u32(rng, (uint32_t)s_pastel_count)];
}

/* Spawn a random shape from content/shapes with a random pastel material */
static void spawn_random_shape(VoxelObjectWorld *world, Bounds3D bounds, RngState *rng)
{
    if (g_voxel_shape_count <= 0)
        return;

    int32_t shape_idx = (int32_t)rng_range_u32(rng, (uint32_t)g_voxel_shape_count);
    const VoxelShape *shape = voxel_shape_get(shape_idx);
    if (!shape)
        return;

    uint8_t pastel_mat = pick_pastel_material(rng);

    /* Remap shape voxels to use the pastel material */
    int32_t total = shape->size_x * shape->size_y * shape->size_z;
    uint8_t *remapped = (uint8_t *)malloc((size_t)total);
    if (!remapped)
        return;

    for (int32_t i = 0; i < total; i++)
    {
        remapped[i] = shape->voxels[i] ? pastel_mat : 0;
    }

    /* Random spawn position within upper portion of bounds */
    float x_range = bounds.max_x - bounds.min_x - 4.0f;
    float z_range = bounds.max_z - bounds.min_z - 4.0f;
    float x = bounds.min_x + 2.0f + rng_float(rng) * x_range;
    float y = bounds.max_y - 2.0f;
    float z = bounds.min_z + 2.0f + rng_float(rng) * z_range;

    Vec3 origin = vec3_create(x, y, z);
    Vec3 velocity = vec3_zero();

    voxel_object_world_add_from_voxels(world, remapped,
                                       shape->size_x, shape->size_y, shape->size_z,
                                       origin, world->voxel_size, velocity, rng);
    free(remapped);
}

/* Create terrain floor */
static void create_terrain_floor(VoxelVolume *vol, float floor_thickness)
{
    Vec3 min_corner = vec3_create(vol->bounds.min_x, vol->bounds.min_y,
                                  vol->bounds.min_z);
    Vec3 max_corner = vec3_create(vol->bounds.max_x, vol->bounds.min_y + floor_thickness,
                                  vol->bounds.max_z);

    volume_fill_box(vol, min_corner, max_corner, MAT_CLOUD);
}

static void ball_pit_init(Scene *scene)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    const BallPitParams *p = &data->params;

    /* Get scene descriptor (single source of truth for dimensions) */
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);

    /* Create terrain volume using scene descriptor's chunk dimensions */
    Vec3 origin = vec3_create(scene->bounds.min_x, scene->bounds.min_y, scene->bounds.min_z);
    data->terrain = volume_create_dims(desc->chunks_x, desc->chunks_y, desc->chunks_z,
                                       origin, data->voxel_size);

    /* Create floor from cloud-colored terrain */
    create_terrain_floor(data->terrain, 0.5f);
    volume_rebuild_all_occupancy(data->terrain);

    /* Create voxel object world using same bounds */
    data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
    voxel_object_world_set_terrain(data->objects, data->terrain);

    /* Create particle system */
    data->particles = particle_system_create(scene->bounds);

    /* DEBUG: Add a large pink cube in the center to verify rendering */
    Vec3 cube_center = vec3_create(0.0f, 3.0f, 0.0f);
    Vec3 cube_half = vec3_create(1.0f, 1.0f, 1.0f);
    voxel_object_world_add_box(data->objects, cube_center, cube_half, MAT_PINK, &scene->rng);

    /* Spawn initial objects */
    int32_t spawn_target = p->initial_spawns;
    const char *stress_env = getenv("PATCH_STRESS_OBJECTS");
    if (stress_env)
    {
        int32_t stress_count = atoi(stress_env);
        if (stress_count > 0 && stress_count <= p->max_spawns)
            spawn_target = stress_count;
    }

    for (int32_t i = 0; i < spawn_target; i++)
    {
        spawn_random_shape(data->objects, scene->bounds, &scene->rng);
    }

    data->stats.spawn_count = spawn_target;
    data->spawn_timer = p->spawn_interval;
}

static void ball_pit_destroy_impl(Scene *scene)
{
    BallPitData *data = (BallPitData *)scene->user_data;

    if (data->particles)
        particle_system_destroy(data->particles);
    if (data->objects)
        voxel_object_world_destroy(data->objects);
    if (data->terrain)
        volume_destroy(data->terrain);

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

    /* Spawn new shapes on timer */
    const BallPitParams *p = &data->params;
    data->spawn_timer -= dt;
    if (data->spawn_timer <= 0.0f && data->stats.spawn_count < p->max_spawns)
    {
        PROFILE_BEGIN(PROFILE_PROP_SPAWN);
        for (int32_t i = 0; i < p->spawn_batch; i++)
        {
            spawn_random_shape(data->objects, scene->bounds, &scene->rng);
        }
        data->stats.spawn_count += p->spawn_batch;
        data->spawn_timer = p->spawn_interval;
        PROFILE_END(PROFILE_PROP_SPAWN);
    }

    /* Update physics */
    PROFILE_BEGIN(PROFILE_SIM_PHYSICS);
    voxel_body_world_update(data->objects, dt);
    PROFILE_END(PROFILE_SIM_PHYSICS);

    /* Update particles */
    PROFILE_BEGIN(PROFILE_SIM_PARTICLES);
    particle_system_update(data->particles, dt);
    PROFILE_END(PROFILE_SIM_PARTICLES);

    PlatformTime t1 = platform_time_now();
    data->stats.tick_time_us = platform_time_delta_seconds(t0, t1) * 1000000.0f;

    PROFILE_END(PROFILE_SIM_TICK);
}

static void ball_pit_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    (void)mouse_x;
    (void)mouse_y;

    /* Left click: destroy voxels and spawn debris particles */
    if (left_down)
    {
        VoxelObjectHit hit = voxel_object_world_raycast(data->objects,
                                                         data->ray_origin, data->ray_dir);
        if (hit.hit)
        {
            /* Destroy voxels and collect their positions/materials */
            #define MAX_DESTROYED 64
            Vec3 destroyed_positions[MAX_DESTROYED];
            uint8_t destroyed_materials[MAX_DESTROYED];

            /* Destroy radius ~1.5 voxels */
            float destroy_radius = data->objects->voxel_size * 1.5f;
            int32_t destroyed = voxel_object_destroy_at_point(
                data->objects, hit.object_index, hit.impact_point, destroy_radius,
                destroyed_positions, destroyed_materials, MAX_DESTROYED);

            /* Spawn one particle per destroyed voxel */
            float voxel_size = data->objects->voxel_size;
            for (int32_t i = 0; i < destroyed; i++)
            {
                Vec3 color = material_get_color(destroyed_materials[i]);
                Vec3 dir = vec3_sub(destroyed_positions[i], hit.impact_point);
                float dist = vec3_length(dir);
                if (dist > 0.001f)
                    dir = vec3_scale(dir, 1.0f / dist);
                else
                    dir = vec3_create(0.0f, 1.0f, 0.0f);

                /* Outward velocity with some randomness */
                float speed = 2.0f + rng_float(&scene->rng) * 2.0f;
                Vec3 velocity = vec3_scale(dir, speed);
                velocity.y += 1.0f;

                particle_system_add(data->particles, &scene->rng,
                                    destroyed_positions[i], velocity, color,
                                    voxel_size * 0.4f);
            }
            #undef MAX_DESTROYED
        }
    }

    /* Right click: spawn a new random shape */
    if (right_down && data->stats.spawn_count < data->params.max_spawns)
    {
        spawn_random_shape(data->objects, scene->bounds, &scene->rng);
        data->stats.spawn_count++;
    }
}

static const char *ball_pit_get_name(Scene *scene)
{
    (void)scene;
    return "Ball Pit";
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
    data->terrain = NULL;
    data->objects = NULL;
    data->particles = NULL;

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

VoxelVolume *ball_pit_get_terrain(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((BallPitData *)scene->user_data)->terrain;
}

VoxelObjectWorld *ball_pit_get_objects(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((BallPitData *)scene->user_data)->objects;
}

ParticleSystem *ball_pit_get_particles(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((BallPitData *)scene->user_data)->particles;
}
