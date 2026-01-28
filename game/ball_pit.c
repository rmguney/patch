#include "game/ball_pit.h"
#include "engine/platform/platform.h"
#include "engine/core/profile.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/sim/detach.h"
#include "engine/physics/collision_object.h"
#include "content/voxel_shapes.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const uint8_t s_pastel_materials[] = {
    MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
    MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE};
static const int32_t s_pastel_count = sizeof(s_pastel_materials) / sizeof(s_pastel_materials[0]);

static uint8_t pick_pastel_material(RngState *rng)
{
    return s_pastel_materials[rng_range_u32(rng, (uint32_t)s_pastel_count)];
}

static void spawn_gary_on_floor(VoxelObjectWorld *world, Bounds3D bounds, float floor_y)
{
    const VoxelShape *gary = voxel_shape_get(SHAPE_GARY);
    if (!gary)
        return;

    int32_t total = gary->size_x * gary->size_y * gary->size_z;
    uint8_t *voxels = (uint8_t *)malloc((size_t)total);
    if (!voxels)
        return;

    memcpy(voxels, gary->voxels, (size_t)total);

    float cx = (bounds.min_x + bounds.max_x) * 0.5f;
    float cz = (bounds.min_z + bounds.max_z) * 0.5f;
    float x = cx + 3.0f;
    float y = floor_y;
    float z = cz + 3.0f;

    Vec3 origin = vec3_create(x, y, z);

    voxel_object_world_add_from_voxels(world, voxels,
                                       gary->size_x, gary->size_y, gary->size_z,
                                       origin, world->voxel_size);

    free(voxels);
}

static void spawn_random_shape(VoxelObjectWorld *world, Bounds3D bounds, RngState *rng)
{
    if (g_voxel_shape_count <= 0)
        return;

    int32_t shape_idx = (int32_t)rng_range_u32(rng, (uint32_t)g_voxel_shape_count);
    const VoxelShape *shape = voxel_shape_get(shape_idx);
    if (!shape)
        return;

    uint8_t pastel_mat = pick_pastel_material(rng);

    int32_t total = shape->size_x * shape->size_y * shape->size_z;
    uint8_t *remapped = (uint8_t *)malloc((size_t)total);
    if (!remapped)
        return;

    for (int32_t i = 0; i < total; i++)
    {
        remapped[i] = shape->voxels[i] ? pastel_mat : 0;
    }

    float x_range = bounds.max_x - bounds.min_x - 4.0f;
    float z_range = bounds.max_z - bounds.min_z - 4.0f;
    float x = bounds.min_x + 2.0f + rng_float(rng) * x_range;
    float y = bounds.max_y - 2.0f;
    float z = bounds.min_z + 2.0f + rng_float(rng) * z_range;

    Vec3 origin = vec3_create(x, y, z);

    voxel_object_world_add_from_voxels(world, remapped,
                                       shape->size_x, shape->size_y, shape->size_z,
                                       origin, world->voxel_size);
    free(remapped);
}

static void throw_random_shape(BallPitData *data, Scene *scene, Vec3 target)
{
    if (g_voxel_shape_count <= 0 || !data->objects || !data->physics)
        return;

    int32_t shape_idx = (int32_t)rng_range_u32(&scene->rng, (uint32_t)g_voxel_shape_count);
    const VoxelShape *shape = voxel_shape_get(shape_idx);
    if (!shape)
        return;

    uint8_t pastel_mat = pick_pastel_material(&scene->rng);

    int32_t total = shape->size_x * shape->size_y * shape->size_z;
    uint8_t *remapped = (uint8_t *)malloc((size_t)total);
    if (!remapped)
        return;

    for (int32_t i = 0; i < total; i++)
    {
        remapped[i] = shape->voxels[i] ? pastel_mat : 0;
    }

    Vec3 spawn_pos = vec3_add(data->ray_origin, vec3_scale(data->ray_dir, 0.5f));
    int32_t obj_idx = voxel_object_world_add_from_voxels(data->objects, remapped,
                                                          shape->size_x, shape->size_y, shape->size_z,
                                                          spawn_pos, data->objects->voxel_size);
    free(remapped);

    if (obj_idx < 0)
        return;

    physics_world_sync_objects(data->physics);

    int32_t body_idx = physics_world_find_body_for_object(data->physics, obj_idx);
    if (body_idx >= 0)
    {
        Vec3 throw_dir = vec3_sub(target, spawn_pos);
        float dist = vec3_length(throw_dir);
        if (dist > 0.001f)
            throw_dir = vec3_scale(throw_dir, 1.0f / dist);
        else
            throw_dir = data->ray_dir;

        float throw_speed = 15.0f;
        Vec3 velocity = vec3_scale(throw_dir, throw_speed);
        physics_body_set_velocity(data->physics, body_idx, velocity);

        Vec3 spin = vec3_create(
            (rng_float(&scene->rng) - 0.5f) * 10.0f,
            (rng_float(&scene->rng) - 0.5f) * 10.0f,
            (rng_float(&scene->rng) - 0.5f) * 10.0f);
        physics_body_set_angular_velocity(data->physics, body_idx, spin);
    }

    data->stats.spawn_count++;
}

static void create_terrain_floor(VoxelVolume *vol, float floor_thickness)
{
    Vec3 min_corner = vec3_create(vol->bounds.min_x, vol->bounds.min_y,
                                  vol->bounds.min_z);
    Vec3 max_corner = vec3_create(vol->bounds.max_x, vol->bounds.min_y + floor_thickness,
                                  vol->bounds.max_z);

    volume_fill_box(vol, min_corner, max_corner, MAT_WHITE);
}

static void create_terrain_features(VoxelVolume *vol, float floor_y)
{
    float cx = (vol->bounds.min_x + vol->bounds.max_x) * 0.5f;
    float cz = (vol->bounds.min_z + vol->bounds.max_z) * 0.5f;
    float wall_height = 4.0f;

    /* Chrome floor section (metallic material) */
    Vec3 chrome_min = vec3_create(cx - 2.0f, floor_y - 0.1f, cz - 2.0f);
    Vec3 chrome_max = vec3_create(cx + 2.0f, floor_y + 0.1f, cz + 2.0f);
    volume_fill_box(vol, chrome_min, chrome_max, MAT_CHROME);

    /* Yellow pillar in center */
    float pillar_size = 0.5f;
    Vec3 pillar_min = vec3_create(cx - pillar_size, floor_y, cz - pillar_size);
    Vec3 pillar_max = vec3_create(cx + pillar_size, floor_y + wall_height, cz + pillar_size);
    volume_fill_box(vol, pillar_min, pillar_max, MAT_YELLOW);

    /* Red wall (front) */
    Vec3 front_wall_min = vec3_create(vol->bounds.min_x, floor_y, vol->bounds.min_z);
    Vec3 front_wall_max = vec3_create(vol->bounds.max_x, floor_y + wall_height, vol->bounds.min_z + 0.5f);
    volume_fill_box(vol, front_wall_min, front_wall_max, MAT_RED);

    /* Green wall (left) */
    Vec3 left_wall_min = vec3_create(vol->bounds.min_x, floor_y, vol->bounds.min_z);
    Vec3 left_wall_max = vec3_create(vol->bounds.min_x + 0.5f, floor_y + wall_height, vol->bounds.max_z);
    volume_fill_box(vol, left_wall_min, left_wall_max, MAT_GREEN);

    /* Emissive glow blocks */
    /* Corner light near red/green wall intersection */
    Vec3 glow1_min = vec3_create(vol->bounds.min_x + 0.6f, floor_y + 1.0f, vol->bounds.min_z + 0.6f);
    Vec3 glow1_max = vec3_create(vol->bounds.min_x + 1.2f, floor_y + 1.6f, vol->bounds.min_z + 1.2f);
    volume_fill_box(vol, glow1_min, glow1_max, MAT_GLOW);

    /* Second glow block on the green wall */
    Vec3 glow2_min = vec3_create(vol->bounds.min_x + 0.6f, floor_y + 2.0f, cz - 0.3f);
    Vec3 glow2_max = vec3_create(vol->bounds.min_x + 1.0f, floor_y + 2.6f, cz + 0.3f);
    volume_fill_box(vol, glow2_min, glow2_max, MAT_GLOW);

    /* Third glow block on the red wall */
    Vec3 glow3_min = vec3_create(cx - 0.3f, floor_y + 2.0f, vol->bounds.min_z + 0.6f);
    Vec3 glow3_max = vec3_create(cx + 0.3f, floor_y + 2.6f, vol->bounds.min_z + 1.0f);
    volume_fill_box(vol, glow3_min, glow3_max, MAT_GLOW);
}

static void ball_pit_init(Scene *scene)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    const BallPitParams *p = &data->params;

    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);

    Vec3 origin = vec3_create(scene->bounds.min_x, scene->bounds.min_y, scene->bounds.min_z);
    data->terrain = volume_create_dims(desc->chunks_x, desc->chunks_y, desc->chunks_z,
                                       origin, data->voxel_size);

    create_terrain_floor(data->terrain, 0.5f);
    create_terrain_features(data->terrain, data->terrain->bounds.min_y + 0.5f);

    volume_rebuild_all_occupancy(data->terrain);

    data->detach_ready = connectivity_work_init(&data->detach_work, data->terrain);

    data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
    voxel_object_world_set_terrain(data->objects, data->terrain);

    float floor_y = data->terrain->bounds.min_y + 0.5f;
    spawn_gary_on_floor(data->objects, scene->bounds, floor_y);

    data->particles = particle_system_create(scene->bounds);
    data->physics = physics_world_create(data->objects, data->terrain);

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

    if (data->physics)
        physics_world_destroy(data->physics);
    if (data->particles)
        particle_system_destroy(data->particles);
    if (data->objects)
        voxel_object_world_destroy(data->objects);

    connectivity_work_destroy(&data->detach_work);

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

    PROFILE_BEGIN(PROFILE_SIM_PARTICLES);
    particle_system_update(data->particles, dt, data->terrain, data->objects);
    PROFILE_END(PROFILE_SIM_PARTICLES);

    /* Process deferred voxel object work (budgeted per-frame) */
    if (data->objects)
    {
        voxel_object_world_process_splits(data->objects);
        voxel_object_world_process_recalcs(data->objects);
        voxel_object_world_tick_render_delays(data->objects);
        voxel_object_world_update_raycast_grid(data->objects);
    }

    if (data->physics)
    {
        physics_world_sync_objects(data->physics);
        physics_world_step(data->physics, dt);
    }

    PlatformTime t1 = platform_time_now();
    data->stats.tick_time_us = platform_time_delta_seconds(t0, t1) * 1000000.0f;

    PROFILE_END(PROFILE_SIM_TICK);
}

static void ball_pit_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    BallPitData *data = (BallPitData *)scene->user_data;
    (void)mouse_x;
    (void)mouse_y;

    static bool right_was_down = false;
    if (right_down && !right_was_down)
    {
        Vec3 target = vec3_add(data->ray_origin, vec3_scale(data->ray_dir, 20.0f));

        float terrain_dist = -1.0f;
        Vec3 terrain_hit_pos = vec3_zero();
        Vec3 terrain_hit_normal = vec3_zero();
        uint8_t terrain_mat = 0;

        if (data->terrain)
        {
            terrain_dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir,
                                          100.0f, &terrain_hit_pos, &terrain_hit_normal, &terrain_mat);
            if (terrain_dist >= 0.0f && terrain_mat != 0)
                target = terrain_hit_pos;
        }

        throw_random_shape(data, scene, target);
    }
    right_was_down = right_down;

    if (left_down)
    {
        VoxelObjectHit obj_hit = {0};
        float obj_dist = 1e30f;

        bool terrain_hit = false;
        float terrain_dist = 1e30f;
        Vec3 terrain_hit_pos = vec3_zero();
        Vec3 terrain_hit_normal = vec3_zero();
        uint8_t terrain_mat = 0;

        if (data->objects)
        {
            obj_hit = voxel_object_world_raycast(data->objects, data->ray_origin, data->ray_dir);
            if (obj_hit.hit)
            {
                Vec3 delta = vec3_sub(obj_hit.impact_point, data->ray_origin);
                obj_dist = vec3_length(delta);
            }
        }

        if (data->terrain)
        {
            terrain_dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir,
                                          100.0f, &terrain_hit_pos, &terrain_hit_normal, &terrain_mat);
            terrain_hit = (terrain_dist >= 0.0f && terrain_mat != 0);
        }

        if (obj_hit.hit && (!terrain_hit || obj_dist <= terrain_dist))
        {
#define MAX_DESTROYED 64
            Vec3 destroyed_positions[MAX_DESTROYED];
            uint8_t destroyed_materials[MAX_DESTROYED];

            float destroy_radius = data->objects->voxel_size * 1.5f;
            int32_t destroyed = detach_object_at_point(
                data->objects, obj_hit.object_index, obj_hit.impact_point, destroy_radius,
                destroyed_positions, destroyed_materials, MAX_DESTROYED);
            int32_t destroyed_cap = destroyed < MAX_DESTROYED ? destroyed : MAX_DESTROYED;

            float voxel_size = data->objects->voxel_size;
            for (int32_t i = 0; i < destroyed_cap; i++)
            {
                Vec3 color = material_get_color(destroyed_materials[i]);
                Vec3 dir = vec3_sub(destroyed_positions[i], obj_hit.impact_point);
                float dist = vec3_length(dir);
                if (dist > 0.001f)
                    dir = vec3_scale(dir, 1.0f / dist);
                else
                    dir = vec3_create(0.0f, 1.0f, 0.0f);

                float speed = 2.0f + rng_float(&scene->rng) * 2.0f;
                Vec3 velocity = vec3_scale(dir, speed);
                velocity.y += 1.0f;

                particle_system_add(data->particles, &scene->rng,
                                    destroyed_positions[i], velocity, color,
                                    voxel_size * 0.4f);
            }
#undef MAX_DESTROYED
        }

        if (terrain_hit && (!obj_hit.hit || terrain_dist < obj_dist) && data->terrain)
        {
            float destroy_radius = data->terrain->voxel_size * 2.0f;

#define MAX_TERRAIN_DESTROYED 64
            Vec3 destroyed_positions[MAX_TERRAIN_DESTROYED];
            Vec3 destroyed_colors[MAX_TERRAIN_DESTROYED];
            int32_t destroyed_count = 0;

            volume_edit_begin(data->terrain);
            for (float dx = -destroy_radius; dx <= destroy_radius; dx += data->terrain->voxel_size)
            {
                for (float dy = -destroy_radius; dy <= destroy_radius; dy += data->terrain->voxel_size)
                {
                    for (float dz = -destroy_radius; dz <= destroy_radius; dz += data->terrain->voxel_size)
                    {
                        Vec3 pos = vec3_create(terrain_hit_pos.x + dx,
                                               terrain_hit_pos.y + dy,
                                               terrain_hit_pos.z + dz);
                        float dist_sq = dx * dx + dy * dy + dz * dz;
                        if (dist_sq <= destroy_radius * destroy_radius)
                        {
                            uint8_t mat = volume_get_at(data->terrain, pos);
                            if (mat != 0)
                            {
                                volume_edit_set(data->terrain, pos, 0);

                                if (destroyed_count < MAX_TERRAIN_DESTROYED)
                                {
                                    destroyed_positions[destroyed_count] = pos;
                                    destroyed_colors[destroyed_count] = material_get_color(mat);
                                    destroyed_count++;
                                }
                            }
                        }
                    }
                }
            }
            volume_edit_end(data->terrain);

            if (data->physics)
                physics_world_wake_in_region(data->physics, terrain_hit_pos, destroy_radius * 2.0f);

            for (int32_t i = 0; i < destroyed_count; i++)
            {
                Vec3 dir = vec3_sub(destroyed_positions[i], terrain_hit_pos);
                float d = vec3_length(dir);
                if (d > 0.001f)
                    dir = vec3_scale(dir, 1.0f / d);
                else
                    dir = vec3_create(0.0f, 1.0f, 0.0f);

                float speed = 2.0f + rng_float(&scene->rng) * 2.0f;
                Vec3 velocity = vec3_scale(dir, speed);
                velocity.y += 1.0f;

                particle_system_add(data->particles, &scene->rng,
                                    destroyed_positions[i], velocity, destroyed_colors[i],
                                    data->terrain->voxel_size * 0.4f);
            }
#undef MAX_TERRAIN_DESTROYED

            /* Mark for deferred connectivity analysis (runs when mouse released) */
            data->pending_connectivity = true;
            data->last_destroy_point = terrain_hit_pos;
        }
    }
    else
    {
        /* Mouse not held - run pending connectivity analysis with throttling */
        if (data->pending_connectivity && data->detach_ready && data->terrain && data->objects)
        {
            int64_t now_ticks = platform_get_ticks();
            int64_t freq = platform_get_frequency();
            double now = (double)now_ticks / (double)freq;
            double cooldown_sec = 0.016; /* ~1 frame minimum between analyses */

            /* Throttle: skip if too soon after last analysis */
            bool cooldown_ok = (now - data->last_connectivity_time) >= cooldown_sec;

            if (cooldown_ok)
            {
                DetachConfig cfg = detach_config_default();
                DetachResult detach_result;
                detach_terrain_process(data->terrain, data->objects, &cfg, &data->detach_work, &detach_result);

                /* Apply outward velocity to newly spawned islands */
                if (detach_result.bodies_spawned > 0 && data->physics)
                {
                    physics_world_sync_objects(data->physics);

                    int32_t count = detach_result.bodies_spawned;
                    if (count > DETACH_MAX_SPAWNED)
                        count = DETACH_MAX_SPAWNED;

                    for (int32_t i = 0; i < count; i++)
                    {
                        int32_t obj_idx = detach_result.spawned_indices[i];
                        VoxelObject *obj = &data->objects->objects[obj_idx];
                        if (!obj->active)
                            continue;

                        int32_t body_idx = physics_world_find_body_for_object(data->physics, obj_idx);
                        if (body_idx < 0)
                            continue;

                        Vec3 dir = vec3_sub(obj->position, data->last_destroy_point);
                        float dist = vec3_length(dir);
                        if (dist > 0.001f)
                            dir = vec3_scale(dir, 1.0f / dist);
                        else
                            dir = vec3_create(0.0f, 1.0f, 0.0f);

                        float speed = 3.0f;
                        Vec3 velocity = vec3_scale(dir, speed);
                        velocity.y += 1.5f;
                        physics_body_set_velocity(data->physics, body_idx, velocity);

                        RigidBody *body = physics_world_get_body(data->physics, body_idx);
                        if (body)
                        {
                            body->flags &= ~PHYS_FLAG_GROUNDED;
                            body->ground_frames = 0;
                        }
                    }
                }

                data->pending_connectivity = false;
                data->last_connectivity_time = now;
            }
            /* If over budget, keep pending_connectivity true to retry next frame */
        }
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
    p.max_spawns = 0; /* Automatic spawning disabled - use right-click to throw objects */
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

PhysicsWorld *ball_pit_get_physics(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((BallPitData *)scene->user_data)->physics;
}
