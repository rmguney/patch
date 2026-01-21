#include "roam.h"
#include "content/scenes.h"
#include "content/materials.h"
#include "engine/core/rng.h"
#include <stdlib.h>
#include <math.h>

static const uint8_t PASTEL_MATERIALS[] = {
    MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
    MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE
};
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

    float base_height = 2.0f;

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
                if (depth < voxel_size * 1.5f)
                {
                    mat = MAT_GRASS;
                }
                else if (depth < voxel_size * 4.0f)
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
        if (y < voxel_size * 2.0f)
            r = radius * 1.2f;
        if (y > height - voxel_size * 2.0f)
            r = radius * 0.8f;

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

static void generate_ruin_wall(VoxelVolume *vol, Vec3 base, float length, float height, float thickness,
                               bool along_x, uint8_t material, float voxel_size, RngState *rng)
{
    for (float y = 0.0f; y < height; y += voxel_size)
    {
        float decay = (y / height) * 0.5f;

        for (float l = 0.0f; l < length; l += voxel_size)
        {
            if (rng_float(rng) < decay)
                continue;

            for (float t = 0.0f; t < thickness; t += voxel_size)
            {
                Vec3 pos;
                if (along_x)
                {
                    pos = vec3_create(base.x + l, base.y + y, base.z + t);
                }
                else
                {
                    pos = vec3_create(base.x + t, base.y + y, base.z + l);
                }
                volume_set_at(vol, pos, material);
            }
        }
    }
}

static void generate_structures(RoamData *data, const SceneDescriptor *desc)
{
    VoxelVolume *vol = data->terrain;
    float voxel_size = data->voxel_size;
    RngState rng;
    rng_seed(&rng, desc->rng_seed + 12345);

    float margin = 2.0f;
    float area_min_x = vol->bounds.min_x + margin;
    float area_max_x = vol->bounds.max_x - margin;
    float area_min_z = vol->bounds.min_z + margin;
    float area_max_z = vol->bounds.max_z - margin;

    for (int32_t i = 0; i < data->params.num_pillars; i++)
    {
        float x = rng_range_f32(&rng, area_min_x, area_max_x);
        float z = rng_range_f32(&rng, area_min_z, area_max_z);
        float base_y = 2.0f + terrain_height(x, z, data->params.terrain_amplitude, data->params.terrain_frequency, desc->rng_seed);

        float height = rng_range_f32(&rng, 3.0f, 8.0f);
        float radius = rng_range_f32(&rng, 0.3f, 0.6f);
        uint8_t mat = PASTEL_MATERIALS[rng_range_u32(&rng, (uint32_t)PASTEL_COUNT)];

        Vec3 base = vec3_create(x, base_y, z);
        generate_pillar(vol, base, height, radius, mat, voxel_size);
    }

    data->stats.pillar_count = data->params.num_pillars;

    for (int32_t i = 0; i < data->params.num_ruins; i++)
    {
        float x = rng_range_f32(&rng, area_min_x + 2.0f, area_max_x - 2.0f);
        float z = rng_range_f32(&rng, area_min_z + 2.0f, area_max_z - 2.0f);
        float base_y = 2.0f + terrain_height(x, z, data->params.terrain_amplitude, data->params.terrain_frequency, desc->rng_seed);

        uint8_t mat = PASTEL_MATERIALS[rng_range_u32(&rng, (uint32_t)PASTEL_COUNT)];
        float wall_height = rng_range_f32(&rng, 2.0f, 5.0f);
        float wall_length = rng_range_f32(&rng, 2.0f, 4.0f);
        float thickness = voxel_size * 2.0f;

        Vec3 base = vec3_create(x, base_y, z);
        bool along_x = rng_float(&rng) > 0.5f;
        generate_ruin_wall(vol, base, wall_length, wall_height, thickness, along_x, mat, voxel_size, &rng);

        if (rng_float(&rng) > 0.5f)
        {
            Vec3 base2;
            if (along_x)
            {
                base2 = vec3_create(x + wall_length - thickness, base_y, z);
            }
            else
            {
                base2 = vec3_create(x, base_y, z + wall_length - thickness);
            }
            generate_ruin_wall(vol, base2, wall_length * 0.7f, wall_height * 0.8f, thickness, !along_x, mat, voxel_size, &rng);
        }
    }

    data->stats.ruin_count = data->params.num_ruins;
}

static void roam_init(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_ROAM);

    Vec3 origin = vec3_create(scene->bounds.min_x, scene->bounds.min_y, scene->bounds.min_z);
    data->terrain = volume_create_dims(desc->chunks_x, desc->chunks_y, desc->chunks_z, origin, data->voxel_size);

    data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
    data->particles = particle_system_create(scene->bounds);

    generate_terrain(data, desc);
    generate_structures(data, desc);

    volume_rebuild_all_occupancy(data->terrain);
    data->stats.terrain_voxels = data->terrain->total_solid_voxels;
}

static void roam_destroy_impl(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    if (data)
    {
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
        vel.x = normal.x * 2.0f + rng_signed_half(rng) * 3.0f;
        vel.y = normal.y * 2.0f + rng_float(rng) * 4.0f + 1.0f;
        vel.z = normal.z * 2.0f + rng_signed_half(rng) * 3.0f;

        float size = data->voxel_size * (0.3f + rng_float(rng) * 0.4f);

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
    float dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir, 100.0f,
                                &hit_pos, &hit_normal, &hit_material);

    if (dist < 0.0f)
        return;

    spawn_destruction_particles(data, hit_pos, hit_normal, hit_material, &scene->rng);

    float radius = data->voxel_size * 3.0f;
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
    p.num_ruins = 8;
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

const RoamStats *roam_get_stats(Scene *scene)
{
    RoamData *data = (RoamData *)scene->user_data;
    return data ? &data->stats : NULL;
}
