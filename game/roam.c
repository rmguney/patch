#include "roam.h"
#include "content/scenes.h"
#include "content/materials.h"
#include "engine/core/rng.h"
#include "engine/physics/collision_object.h"
#include "engine/sim/detach.h"
#include <stdlib.h>
#include <math.h>

#define ROAM_PLAYER_START_HEIGHT 5.0f
#define ROAM_CAMERA_DISTANCE 5.0f
#define ROAM_CAMERA_HEIGHT 2.0f
#define ROAM_CAMERA_LERP_SPEED 8.0f
#define ROAM_MOVE_SPEED 6.0f
#define ROAM_JUMP_VELOCITY 8.0f
#define ROAM_PITCH_MIN -0.8f
#define ROAM_PITCH_MAX 0.8f
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
#define ROAM_PARTICLE_VEL_NORM 2.0f
#define ROAM_PARTICLE_VEL_SPREAD 3.0f
#define ROAM_PARTICLE_VEL_Y_MAX 4.0f
#define ROAM_PARTICLE_VEL_Y_MIN 1.0f
#define ROAM_PARTICLE_SIZE_BASE 0.3f
#define ROAM_PARTICLE_SIZE_VAR 0.4f

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

    data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
    data->particles = particle_system_create(scene->bounds);
    data->physics = NULL;

    generate_terrain(data, desc);
    generate_structures(data, desc);

    volume_rebuild_all_occupancy(data->terrain);
    data->stats.terrain_voxels = data->terrain->total_solid_voxels;

    data->physics = physics_world_create(data->objects, data->terrain);
    connectivity_work_init(&data->detach_work, data->terrain);

    float cx = (scene->bounds.min_x + scene->bounds.max_x) * 0.5f;
    float cz = (scene->bounds.min_z + scene->bounds.max_z) * 0.5f;
    Vec3 start_pos = vec3_create(cx, ROAM_BASE_HEIGHT + ROAM_PLAYER_START_HEIGHT, cz);
    character_init(&data->player, start_pos);

    data->camera_yaw = 0.0f;
    data->camera_pitch = 0.3f;
    data->camera_offset = vec3_create(0.0f, ROAM_CAMERA_HEIGHT, -ROAM_CAMERA_DISTANCE);
    data->camera_position = vec3_add(start_pos, data->camera_offset);
    data->move_input = vec3_zero();
    data->jump_pressed = false;
}

static void roam_destroy_impl(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        if (data->physics)
            physics_world_destroy(data->physics);
        connectivity_work_destroy(&data->detach_work);
        if (data->particles)
            particle_system_destroy(data->particles);
        if (data->objects)
            voxel_object_world_destroy(data->objects);
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

    if (data->particles)
    {
        particle_system_update(data->particles, SIM_TIMESTEP);
    }

    if (data->objects)
    {
        voxel_object_world_process_splits(data->objects);
        voxel_object_world_process_recalcs(data->objects);
        voxel_object_world_update_raycast_grid(data->objects);
    }

    if (data->terrain && data->objects)
    {
        DetachConfig cfg = detach_config_default();
        detach_terrain_process(data->terrain, data->objects, &cfg, &data->detach_work, NULL);
    }

    if (data->physics)
    {
        physics_world_sync_objects(data->physics);
        physics_world_step(data->physics, SIM_TIMESTEP);
        physics_process_object_collisions(data->physics, SIM_TIMESTEP);
    }

    float cos_yaw = cosf(data->camera_yaw);
    float sin_yaw = sinf(data->camera_yaw);

    Vec3 forward = vec3_create(sin_yaw, 0.0f, cos_yaw);
    Vec3 right = vec3_create(cos_yaw, 0.0f, -sin_yaw);

    Vec3 world_move = vec3_zero();
    world_move = vec3_add(world_move, vec3_scale(forward, data->move_input.z * ROAM_MOVE_SPEED));
    world_move = vec3_add(world_move, vec3_scale(right, data->move_input.x * ROAM_MOVE_SPEED));

    character_move(&data->player, data->terrain, data->objects, world_move, SIM_TIMESTEP);

    if (data->jump_pressed && data->player.is_grounded)
    {
        character_jump(&data->player, ROAM_JUMP_VELOCITY);
        data->jump_pressed = false;
    }

    Vec3 player_head = character_get_head_position(&data->player);

    float cam_cos_pitch = cosf(data->camera_pitch);
    float cam_sin_pitch = sinf(data->camera_pitch);

    Vec3 cam_back = vec3_create(
        -sin_yaw * cam_cos_pitch,
        cam_sin_pitch,
        -cos_yaw * cam_cos_pitch);

    Vec3 target_cam_pos = vec3_add(player_head, vec3_scale(cam_back, ROAM_CAMERA_DISTANCE));
    target_cam_pos.y += ROAM_CAMERA_HEIGHT * 0.5f;

    float lerp_factor = 1.0f - expf(-ROAM_CAMERA_LERP_SPEED * SIM_TIMESTEP);
    data->camera_position = vec3_lerp(data->camera_position, target_cam_pos, lerp_factor);

    data->stats.particles_active = data->particles ? data->particles->count : 0;
}

static void spawn_destruction_particles(RoamData *data, Vec3 pos, Vec3 normal, uint8_t material, RngState *rng)
{
    if (!data->particles)
        return;

    Vec3 color = material_get_color(material);
    int32_t count = 3 + rng_range_i32(rng, 0, 3);

    for (int32_t i = 0; i < count; i++)
    {
        Vec3 vel;
        vel.x = normal.x * ROAM_PARTICLE_VEL_NORM + rng_signed_half(rng) * ROAM_PARTICLE_VEL_SPREAD;
        vel.y = normal.y * ROAM_PARTICLE_VEL_NORM + rng_float(rng) * ROAM_PARTICLE_VEL_Y_MAX + ROAM_PARTICLE_VEL_Y_MIN;
        vel.z = normal.z * ROAM_PARTICLE_VEL_NORM + rng_signed_half(rng) * ROAM_PARTICLE_VEL_SPREAD;

        float size = data->voxel_size * (ROAM_PARTICLE_SIZE_BASE + rng_float(rng) * ROAM_PARTICLE_SIZE_VAR);

        particle_system_add(data->particles, rng, pos, vel, color, size);
    }
}

static void roam_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    (void)mouse_x;
    (void)mouse_y;
    (void)right_down;

    RoamData *data = (RoamData *)scene->user_data;
    if (!data)
        return;

    bool left_clicked = left_down && !data->left_was_down;
    data->left_was_down = left_down;

    if (!left_clicked)
        return;

    Vec3 hit_pos, hit_normal;
    uint8_t hit_material;
    float dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir, ROAM_RAYCAST_MAX_DIST,
                                &hit_pos, &hit_normal, &hit_material);

    if (dist < 0.0f)
        return;

    spawn_destruction_particles(data, hit_pos, hit_normal, hit_material, &scene->rng);

    float radius = data->voxel_size * ROAM_DESTROY_RADIUS_MULT;
    volume_edit_begin(data->terrain);

    for (float dx = -radius; dx <= radius; dx += data->voxel_size)
    {
        for (float dy = -radius; dy <= radius; dy += data->voxel_size)
        {
            for (float dz = -radius; dz <= radius; dz += data->voxel_size)
            {
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= radius * radius)
                {
                    Vec3 vpos = vec3_create(hit_pos.x + dx, hit_pos.y + dy, hit_pos.z + dz);
                    volume_edit_set(data->terrain, vpos, MAT_AIR);
                }
            }
        }
    }

    volume_edit_end(data->terrain);
    data->stats.terrain_voxels = data->terrain->total_solid_voxels;
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
    p.num_pillars = 15;
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
    data->use_ortho_camera = true;
    data->left_was_down = false;

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

bool roam_get_ortho_camera(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? data->use_ortho_camera : true;
}

void roam_set_ortho_camera(Scene *scene, bool ortho)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        data->use_ortho_camera = ortho;
    }
}

void roam_toggle_camera(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        data->use_ortho_camera = !data->use_ortho_camera;
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

void roam_set_move_input(Scene *scene, float x, float z)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        data->move_input.x = x;
        data->move_input.z = z;
    }
}

void roam_set_look_input(Scene *scene, float yaw_delta, float pitch_delta)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (!data)
        return;

    data->camera_yaw += yaw_delta;
    data->camera_pitch += pitch_delta;

    if (data->camera_pitch < ROAM_PITCH_MIN)
        data->camera_pitch = ROAM_PITCH_MIN;
    if (data->camera_pitch > ROAM_PITCH_MAX)
        data->camera_pitch = ROAM_PITCH_MAX;
}

void roam_set_jump(Scene *scene, bool pressed)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
        data->jump_pressed = pressed;
    }
}

Vec3 roam_get_camera_position(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? data->camera_position : vec3_zero();
}

Vec3 roam_get_camera_target(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (!data)
        return vec3_zero();
    return character_get_head_position(&data->player);
}

Character *roam_get_player(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? &data->player : NULL;
}
