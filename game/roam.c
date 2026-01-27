#include "roam.h"
#include "content/scenes.h"
#include "content/materials.h"
#include "engine/core/rng.h"
#include "engine/core/profile.h"
#include "engine/voxel/connectivity.h"
#include "engine/sim/detach.h"
#include "engine/platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ROAM_BASE_HEIGHT 2.0f
#define ROAM_GRASS_DEPTH_MULT 1.5f
#define ROAM_DIRT_DEPTH_MULT 4.0f
#define ROAM_PILLAR_BASE_MULT 1.2f
#define ROAM_PILLAR_TOP_MULT 0.8f
#define ROAM_PILLAR_BASE_DEPTH 2.0f
#define ROAM_STRUCTURE_MARGIN 2.0f
#define ROAM_STRUCTURE_SEED 12345
#define ROAM_PILLAR_HEIGHT_MIN 3.0f
#define ROAM_PILLAR_HEIGHT_MAX 8.0f
#define ROAM_PILLAR_RADIUS_MIN 0.3f
#define ROAM_PILLAR_RADIUS_MAX 0.6f
#define ROAM_RAYCAST_MAX_DIST 100.0f
#define ROAM_DESTROY_RADIUS_MULT 3.0f

static const uint8_t PASTEL_MATERIALS[] = {
    MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
    MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE};
static const int32_t PASTEL_COUNT = sizeof(PASTEL_MATERIALS) / sizeof(PASTEL_MATERIALS[0]);

static float noise_hash(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t n = (uint32_t)x + (uint32_t)z * 57 + seed * 131;
    n = (n << 13) ^ n;
    return 1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF) / 1073741824.0f;
}

static float noise_lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static float noise_smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static float noise_2d(float x, float z, uint32_t seed)
{
    int32_t ix = (int32_t)floorf(x);
    int32_t iz = (int32_t)floorf(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;

    float v00 = noise_hash(ix, iz, seed);
    float v10 = noise_hash(ix + 1, iz, seed);
    float v01 = noise_hash(ix, iz + 1, seed);
    float v11 = noise_hash(ix + 1, iz + 1, seed);

    float sx = noise_smooth(fx);
    float sz = noise_smooth(fz);

    float nx0 = noise_lerp(v00, v10, sx);
    float nx1 = noise_lerp(v01, v11, sx);

    return noise_lerp(nx0, nx1, sz);
}

static float terrain_height(float x, float z, float amplitude, float frequency, uint32_t seed)
{
    float height = 0.0f;
    float amp = amplitude;
    float freq = frequency;

    for (int32_t octave = 0; octave < 4; octave++)
    {
        height += noise_2d(x * freq, z * freq, seed + (uint32_t)octave * 1000) * amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }

    return height;
}

static void generate_terrain(RoamData *data, const SceneDescriptor *desc)
{
    VoxelVolume *vol = data->terrain;
    float voxel_size = data->voxel_size;
    uint32_t seed = desc->rng_seed;

    float base_height = ROAM_BASE_HEIGHT;

    for (float x = vol->bounds.min_x; x < vol->bounds.max_x; x += voxel_size)
    {
        for (float z = vol->bounds.min_z; z < vol->bounds.max_z; z += voxel_size)
        {
            float h = terrain_height(x, z, data->params.terrain_amplitude, data->params.terrain_frequency, seed);
            float surface_y = base_height + h;

            for (float y = vol->bounds.min_y; y < surface_y && y < vol->bounds.max_y; y += voxel_size)
            {
                Vec3 pos = vec3_create(x, y, z);
                float depth = surface_y - y;

                uint8_t mat;
                if (depth < voxel_size * ROAM_GRASS_DEPTH_MULT)
                {
                    mat = MAT_GRASS;
                }
                else if (depth < voxel_size * ROAM_DIRT_DEPTH_MULT)
                {
                    mat = MAT_DIRT;
                }
                else
                {
                    mat = MAT_STONE;
                }

                volume_set_at(vol, pos, mat);
            }
        }
    }
}

static void generate_pillar(VoxelVolume *vol, Vec3 base, float height, float radius, uint8_t material, float voxel_size)
{
    for (float y = 0.0f; y < height; y += voxel_size)
    {
        float r = radius;
        if (y < voxel_size * ROAM_PILLAR_BASE_DEPTH)
            r = radius * ROAM_PILLAR_BASE_MULT;
        if (y > height - voxel_size * ROAM_PILLAR_BASE_DEPTH)
            r = radius * ROAM_PILLAR_TOP_MULT;

        for (float dx = -r; dx <= r; dx += voxel_size)
        {
            for (float dz = -r; dz <= r; dz += voxel_size)
            {
                if (dx * dx + dz * dz <= r * r)
                {
                    Vec3 pos = vec3_create(base.x + dx, base.y + y, base.z + dz);
                    volume_set_at(vol, pos, material);
                }
            }
        }
    }
}

static void generate_structures(RoamData *data, const SceneDescriptor *desc)
{
    VoxelVolume *vol = data->terrain;
    float voxel_size = data->voxel_size;
    RngState rng;
    rng_seed(&rng, desc->rng_seed + ROAM_STRUCTURE_SEED);

    float margin = ROAM_STRUCTURE_MARGIN;
    float area_min_x = vol->bounds.min_x + margin;
    float area_max_x = vol->bounds.max_x - margin;
    float area_min_z = vol->bounds.min_z + margin;
    float area_max_z = vol->bounds.max_z - margin;

    for (int32_t i = 0; i < data->params.num_pillars; i++)
    {
        float x = rng_range_f32(&rng, area_min_x, area_max_x);
        float z = rng_range_f32(&rng, area_min_z, area_max_z);
        float base_y = ROAM_BASE_HEIGHT + terrain_height(x, z, data->params.terrain_amplitude, data->params.terrain_frequency, desc->rng_seed);

        float height = rng_range_f32(&rng, ROAM_PILLAR_HEIGHT_MIN, ROAM_PILLAR_HEIGHT_MAX);
        float radius = rng_range_f32(&rng, ROAM_PILLAR_RADIUS_MIN, ROAM_PILLAR_RADIUS_MAX);
        uint8_t mat = PASTEL_MATERIALS[rng_range_u32(&rng, (uint32_t)PASTEL_COUNT)];

        Vec3 base = vec3_create(x, base_y, z);
        generate_pillar(vol, base, height, radius, mat, voxel_size);
    }

    data->stats.pillar_count = data->params.num_pillars;
}

static void roam_init(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_ROAM);

    Vec3 origin = vec3_create(scene->bounds.min_x, scene->bounds.min_y, scene->bounds.min_z);
    data->terrain = volume_create_dims(desc->chunks_x, desc->chunks_y, desc->chunks_z, origin, data->voxel_size);

    generate_terrain(data, desc);
    generate_structures(data, desc);

    volume_rebuild_all_occupancy(data->terrain);
    data->stats.terrain_voxels = data->terrain->total_solid_voxels;

    ConnectivityWorkBuffer *work = (ConnectivityWorkBuffer *)calloc(1, sizeof(ConnectivityWorkBuffer));
    data->detach_work = work;
    data->detach_ready = work && connectivity_work_init(work, data->terrain);

    data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
    voxel_object_world_set_terrain(data->objects, data->terrain);

    data->particles = particle_system_create(scene->bounds);
    data->physics = physics_world_create(data->objects, data->terrain);
}

static void roam_destroy_impl(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        if (data->physics)
            physics_world_destroy(data->physics);
        if (data->particles)
            particle_system_destroy(data->particles);
        if (data->objects)
            voxel_object_world_destroy(data->objects);
        if (data->detach_work)
        {
            connectivity_work_destroy((ConnectivityWorkBuffer *)data->detach_work);
            free(data->detach_work);
        }
        if (data->terrain)
            volume_destroy(data->terrain);
        free(data);
    }
    free(scene);
}

static void roam_tick(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (!data)
        return;

    float dt = 1.0f / 60.0f;

    if (data->objects)
    {
        voxel_object_world_process_splits(data->objects);
        voxel_object_world_process_recalcs(data->objects);
        voxel_object_world_tick_render_delays(data->objects);
    }

    if (data->physics && data->objects)
    {
        physics_world_sync_objects(data->physics);
        physics_world_step(data->physics, dt);
    }

    if (data->particles)
    {
        particle_system_update(data->particles, dt);
        data->stats.particles_active = data->particles->count;
    }

    if (data->objects)
        voxel_object_world_update_raycast_grid(data->objects);
}

static void roam_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    (void)mouse_x;
    (void)mouse_y;
    (void)right_down;

    RoamData *data = (RoamData *)scene->user_data;
    if (!data || !data->terrain)
        return;

    data->left_was_down = left_down;

    if (left_down)
    {
        /* Raycast against both terrain and objects */
        bool terrain_hit = false;
        float terrain_dist = 1e30f;
        Vec3 terrain_hit_pos = vec3_zero();
        Vec3 terrain_hit_normal = vec3_zero();
        uint8_t terrain_mat = 0;

        VoxelObjectHit obj_hit = {0};
        float obj_dist = 1e30f;

        if (data->objects)
        {
            obj_hit = voxel_object_world_raycast(data->objects, data->ray_origin, data->ray_dir);
            if (obj_hit.hit)
                obj_dist = vec3_length(vec3_sub(obj_hit.impact_point, data->ray_origin));
        }

        terrain_dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir,
                                      ROAM_RAYCAST_MAX_DIST, &terrain_hit_pos, &terrain_hit_normal, &terrain_mat);
        terrain_hit = (terrain_dist >= 0.0f && terrain_mat != 0);

        /* Destroy object voxels */
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
                float d = vec3_length(dir);
                if (d > 0.001f)
                    dir = vec3_scale(dir, 1.0f / d);
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

        /* Destroy terrain voxels */
        if (terrain_hit && (!obj_hit.hit || terrain_dist < obj_dist))
        {
            float destroy_radius = data->terrain->voxel_size * ROAM_DESTROY_RADIUS_MULT;

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

            data->pending_connectivity = true;
            data->last_destroy_point = terrain_hit_pos;
            data->stats.terrain_voxels = data->terrain->total_solid_voxels;
        }
    }
    else
    {
        /* Mouse not held - run pending connectivity analysis */
        if (data->pending_connectivity && data->detach_ready && data->terrain && data->objects)
        {
            int64_t now_ticks = platform_get_ticks();
            int64_t freq = platform_get_frequency();
            double now = (double)now_ticks / (double)freq;
            double cooldown_sec = 0.016;

            bool cooldown_ok = (now - data->last_connectivity_time) >= cooldown_sec;

            if (cooldown_ok)
            {
                DetachConfig cfg = detach_config_default();
                DetachResult detach_result;
                detach_terrain_process(data->terrain, data->objects, &cfg,
                                       (ConnectivityWorkBuffer *)data->detach_work, &detach_result);

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
        }
    }
}

static const char *roam_get_name(Scene *scene)
{
    (void)scene;
    return "Roam";
}

static const SceneVTable roam_vtable = {
    .init = roam_init,
    .destroy = roam_destroy_impl,
    .tick = roam_tick,
    .handle_input = roam_handle_input,
    .render = NULL,
    .get_name = roam_get_name};

RoamParams roam_default_params(void)
{
    RoamParams p;
    p.num_pillars = 60;
    p.terrain_amplitude = 3.0f;
    p.terrain_frequency = 0.1f;
    return p;
}

Scene *roam_scene_create(Bounds3D bounds, float voxel_size, const RoamParams *params)
{
    Scene *scene = (Scene *)calloc(1, sizeof(Scene));
    if (!scene)
        return NULL;

    RoamData *data = (RoamData *)calloc(1, sizeof(RoamData));
    if (!data)
    {
        free(scene);
        return NULL;
    }

    data->params = params ? *params : roam_default_params();
    data->voxel_size = voxel_size;
    data->left_was_down = false;
    data->pending_connectivity = false;
    data->detach_ready = false;
    data->last_connectivity_time = 0.0;

    scene->vtable = &roam_vtable;
    scene->bounds = bounds;
    scene->user_data = data;

    return scene;
}

void roam_scene_destroy(Scene *scene)
{
    if (scene && scene->vtable && scene->vtable->destroy)
    {
        scene->vtable->destroy(scene);
    }
}

void roam_set_ray(Scene *scene, Vec3 origin, Vec3 direction)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        data->ray_origin = origin;
        data->ray_dir = direction;
    }
}

VoxelVolume *roam_get_terrain(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? data->terrain : NULL;
}

VoxelObjectWorld *roam_get_objects(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? data->objects : NULL;
}

ParticleSystem *roam_get_particles(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? data->particles : NULL;
}

PhysicsWorld *roam_get_physics(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? data->physics : NULL;
}

const RoamStats *roam_get_stats(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? &data->stats : NULL;
}
