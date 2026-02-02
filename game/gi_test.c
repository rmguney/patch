#include "game/gi_test.h"
#include "engine/platform/platform.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/physics/collision_object.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Room dimensions in voxels (at 0.1 voxel_size = 10m x 6m x 10m room) */
#define ROOM_X 100
#define ROOM_Y 60
#define ROOM_Z 100
#define WALL_THICKNESS 3

/* Place a solid box of material into the volume (voxel-index coordinates) */
static void fill_box(VoxelVolume *vol, float voxel_size,
                     int32_t x0, int32_t y0, int32_t z0,
                     int32_t x1, int32_t y1, int32_t z1,
                     uint8_t mat)
{
    float bx = vol->bounds.min_x;
    float by = vol->bounds.min_y;
    float bz = vol->bounds.min_z;

    for (int32_t z = z0; z < z1; z++)
    {
        for (int32_t y = y0; y < y1; y++)
        {
            for (int32_t x = x0; x < x1; x++)
            {
                Vec3 pos = vec3_create(
                    bx + ((float)x + 0.5f) * voxel_size,
                    by + ((float)y + 0.5f) * voxel_size,
                    bz + ((float)z + 0.5f) * voxel_size);
                volume_set_at(vol, pos, mat);
            }
        }
    }
}

/* Place a solid sphere of material into the volume (voxel-index coordinates) */
static void fill_sphere(VoxelVolume *vol, float voxel_size,
                        int32_t cx, int32_t cy, int32_t cz,
                        int32_t radius, uint8_t mat)
{
    float bx = vol->bounds.min_x;
    float by = vol->bounds.min_y;
    float bz = vol->bounds.min_z;
    int32_t r2 = radius * radius;

    for (int32_t z = cz - radius; z <= cz + radius; z++)
    {
        for (int32_t y = cy - radius; y <= cy + radius; y++)
        {
            for (int32_t x = cx - radius; x <= cx + radius; x++)
            {
                int32_t dx = x - cx;
                int32_t dy = y - cy;
                int32_t dz = z - cz;
                if (dx * dx + dy * dy + dz * dz <= r2)
                {
                    Vec3 pos = vec3_create(
                        bx + ((float)x + 0.5f) * voxel_size,
                        by + ((float)y + 0.5f) * voxel_size,
                        bz + ((float)z + 0.5f) * voxel_size);
                    volume_set_at(vol, pos, mat);
                }
            }
        }
    }
}

/*
 * Build a Cornell box-style room for GI testing.
 *
 * Layout (looking from -Z toward +Z):
 *   - Floor: stone gray
 *   - Ceiling: cloud white (with opening for light)
 *   - Back wall (+Z): stone gray
 *   - Left wall (-X side): coral red/orange for warm color bleed
 *   - Right wall (+X side): teal/cyan for cool color bleed
 *   - Front wall (-Z): partial opening for camera entry
 *
 * Interior objects:
 *   - Tall box (left side): metal, tests specular + shadow
 *   - Short box (right side): stone, tests diffuse bounce
 *   - Sphere (center-back): mint green, tests curved GI reception
 *   - Small emissive block: orange glow source
 */
static void build_cornell_room(VoxelVolume *vol, float voxel_size)
{
    /* Offset so the room is centered in the volume (volume is 512x128x512 voxels for 16x4x16 chunks) */
    int32_t ox = 206; /* Center room in X: (512 - 100) / 2 */
    int32_t oy = 5;   /* Slightly above bottom */
    int32_t oz = 206;  /* Center room in Z */

    /* Floor */
    fill_box(vol, voxel_size,
             ox, oy, oz,
             ox + ROOM_X, oy + WALL_THICKNESS, oz + ROOM_Z,
             MAT_STONE);

    /* Ceiling with light opening in center third */
    /* Left portion */
    fill_box(vol, voxel_size,
             ox, oy + ROOM_Y - WALL_THICKNESS, oz,
             ox + ROOM_X / 3, oy + ROOM_Y, oz + ROOM_Z,
             MAT_CLOUD);
    /* Right portion */
    fill_box(vol, voxel_size,
             ox + 2 * ROOM_X / 3, oy + ROOM_Y - WALL_THICKNESS, oz,
             ox + ROOM_X, oy + ROOM_Y, oz + ROOM_Z,
             MAT_CLOUD);
    /* Front strip (closing front of opening) */
    fill_box(vol, voxel_size,
             ox + ROOM_X / 3, oy + ROOM_Y - WALL_THICKNESS, oz,
             ox + 2 * ROOM_X / 3, oy + ROOM_Y, oz + ROOM_Z / 3,
             MAT_CLOUD);
    /* Back strip (closing back of opening) */
    fill_box(vol, voxel_size,
             ox + ROOM_X / 3, oy + ROOM_Y - WALL_THICKNESS, oz + 2 * ROOM_Z / 3,
             ox + 2 * ROOM_X / 3, oy + ROOM_Y, oz + ROOM_Z,
             MAT_CLOUD);

    /* Emissive ceiling light panel (inside the opening area) */
    fill_box(vol, voxel_size,
             ox + ROOM_X / 3 + 5, oy + ROOM_Y - WALL_THICKNESS - 1, oz + ROOM_Z / 3 + 5,
             ox + 2 * ROOM_X / 3 - 5, oy + ROOM_Y - WALL_THICKNESS, oz + 2 * ROOM_Z / 3 - 5,
             MAT_ORANGE);

    /* Back wall (+Z) */
    fill_box(vol, voxel_size,
             ox, oy, oz + ROOM_Z - WALL_THICKNESS,
             ox + ROOM_X, oy + ROOM_Y, oz + ROOM_Z,
             MAT_STONE);

    /* Left wall (-X side): warm color for color bleeding */
    fill_box(vol, voxel_size,
             ox, oy, oz,
             ox + WALL_THICKNESS, oy + ROOM_Y, oz + ROOM_Z,
             MAT_RED);

    /* Right wall (+X side): cool color for color bleeding */
    fill_box(vol, voxel_size,
             ox + ROOM_X - WALL_THICKNESS, oy, oz,
             ox + ROOM_X, oy + ROOM_Y, oz + ROOM_Z,
             MAT_GREEN);

    /* Front wall (-Z side) — partial, with opening for camera view */
    /* Left pillar */
    fill_box(vol, voxel_size,
             ox, oy, oz,
             ox + ROOM_X / 4, oy + ROOM_Y, oz + WALL_THICKNESS,
             MAT_STONE);
    /* Right pillar */
    fill_box(vol, voxel_size,
             ox + 3 * ROOM_X / 4, oy, oz,
             ox + ROOM_X, oy + ROOM_Y, oz + WALL_THICKNESS,
             MAT_STONE);
    /* Top beam */
    fill_box(vol, voxel_size,
             ox + ROOM_X / 4, oy + 3 * ROOM_Y / 4, oz,
             ox + 3 * ROOM_X / 4, oy + ROOM_Y, oz + WALL_THICKNESS,
             MAT_STONE);

    /* --- Interior objects --- */

    /* Tall box (left side): metal for specular + shadow testing */
    int32_t tb_x = ox + 20;
    int32_t tb_z = oz + 55;
    fill_box(vol, voxel_size,
             tb_x, oy + WALL_THICKNESS, tb_z,
             tb_x + 15, oy + WALL_THICKNESS + 35, tb_z + 15,
             MAT_METAL);

    /* Short box (right side): stone for diffuse bounce testing */
    int32_t sb_x = ox + 60;
    int32_t sb_z = oz + 35;
    fill_box(vol, voxel_size,
             sb_x, oy + WALL_THICKNESS, sb_z,
             sb_x + 16, oy + WALL_THICKNESS + 18, sb_z + 16,
             MAT_STONE);

    /* Sphere (center-back): shows curved surface GI reception */
    fill_sphere(vol, voxel_size,
                ox + 50, oy + WALL_THICKNESS + 10, oz + 70,
                8, MAT_MINT);

    /* Small emissive block (near left wall): secondary light source */
    fill_box(vol, voxel_size,
             ox + 8, oy + WALL_THICKNESS, oz + 25,
             ox + 14, oy + WALL_THICKNESS + 6, oz + 31,
             MAT_ORANGE);

    /* Glass box (center-left): tests transparency in GI */
    fill_box(vol, voxel_size,
             ox + 35, oy + WALL_THICKNESS, oz + 25,
             ox + 45, oy + WALL_THICKNESS + 20, oz + 35,
             MAT_GLASS);

    /* Ground detail: colored patches on floor to show bounce light */
    /* Pink patch near left wall */
    fill_box(vol, voxel_size,
             ox + 5, oy + WALL_THICKNESS, oz + 15,
             ox + 25, oy + WALL_THICKNESS + 1, oz + 30,
             MAT_PINK);

    /* Lavender patch near right wall */
    fill_box(vol, voxel_size,
             ox + 75, oy + WALL_THICKNESS, oz + 40,
             ox + 95, oy + WALL_THICKNESS + 1, oz + 55,
             MAT_LAVENDER);

    /* Wall for light leak testing (4 voxels thick = 1 radiance voxel) */
    fill_box(vol, voxel_size,
             ox + 44, oy + WALL_THICKNESS, oz + 60,
             ox + 48, oy + WALL_THICKNESS + 25, oz + 80,
             MAT_STONE);
}

static void gi_test_init(Scene *scene)
{
    GITestData *data = (GITestData *)scene->user_data;
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_GI_TEST);

    Vec3 origin = vec3_create(scene->bounds.min_x, scene->bounds.min_y, scene->bounds.min_z);
    data->terrain = volume_create_dims(desc->chunks_x, desc->chunks_y, desc->chunks_z,
                                       origin, data->voxel_size);

    build_cornell_room(data->terrain, data->voxel_size);

    volume_rebuild_all_occupancy(data->terrain);

    data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
    voxel_object_world_set_terrain(data->objects, data->terrain);

    data->particles = particle_system_create(scene->bounds);
    data->physics = physics_world_create(data->objects, data->terrain);
}

static void gi_test_destroy_impl(Scene *scene)
{
    GITestData *data = (GITestData *)scene->user_data;

    if (data->physics)
        physics_world_destroy(data->physics);
    if (data->particles)
        particle_system_destroy(data->particles);
    if (data->objects)
        voxel_object_world_destroy(data->objects);
    if (data->terrain)
        volume_destroy(data->terrain);

    free(data);
    free(scene);
}

static void gi_test_tick(Scene *scene)
{
    GITestData *data = (GITestData *)scene->user_data;

    if (data->objects)
    {
        voxel_object_world_process_splits(data->objects);
        voxel_object_world_process_recalcs(data->objects);
        voxel_object_world_tick_render_delays(data->objects);
        voxel_object_world_update_raycast_grid(data->objects);
    }

    if (data->particles)
    {
        particle_system_update(data->particles, SIM_TIMESTEP, data->terrain, data->objects);
    }

    if (data->physics)
    {
        physics_world_sync_objects(data->physics);
        physics_world_step(data->physics, SIM_TIMESTEP);
    }
}

static void gi_test_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    GITestData *data = (GITestData *)scene->user_data;
    (void)mouse_x;
    (void)mouse_y;
    (void)right_down;

    if (left_down && data->terrain)
    {
        Vec3 terrain_hit_pos = vec3_zero();
        Vec3 terrain_hit_normal = vec3_zero();
        uint8_t terrain_mat = 0;

        float terrain_dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir,
                                            100.0f, &terrain_hit_pos, &terrain_hit_normal, &terrain_mat);

        if (terrain_dist >= 0.0f && terrain_mat != 0)
        {
            float destroy_radius = data->terrain->voxel_size * 2.0f;

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

                                Vec3 color = material_get_color(mat);
                                Vec3 dir = vec3_sub(pos, terrain_hit_pos);
                                float d = vec3_length(dir);
                                if (d > 0.001f)
                                    dir = vec3_scale(dir, 1.0f / d);
                                else
                                    dir = vec3_create(0.0f, 1.0f, 0.0f);

                                float speed = 2.0f + rng_float(&scene->rng) * 2.0f;
                                Vec3 velocity = vec3_scale(dir, speed);
                                velocity.y += 1.0f;

                                particle_system_add(data->particles, &scene->rng,
                                                    pos, velocity, color,
                                                    data->terrain->voxel_size * 0.4f);
                            }
                        }
                    }
                }
            }
            volume_edit_end(data->terrain);
        }
    }
}

static const char *gi_test_get_name(Scene *scene)
{
    (void)scene;
    return "GI Test";
}

static const SceneVTable gi_test_vtable = {
    .init = gi_test_init,
    .destroy = gi_test_destroy_impl,
    .tick = gi_test_tick,
    .handle_input = gi_test_handle_input,
    .get_name = gi_test_get_name};

Scene *gi_test_scene_create(Bounds3D bounds, float voxel_size)
{
    Scene *scene = (Scene *)calloc(1, sizeof(Scene));
    if (!scene)
        return NULL;

    GITestData *data = (GITestData *)calloc(1, sizeof(GITestData));
    if (!data)
    {
        free(scene);
        return NULL;
    }

    data->voxel_size = voxel_size;
    data->ray_origin = vec3_zero();
    data->ray_dir = vec3_create(0.0f, 0.0f, -1.0f);

    scene->vtable = &gi_test_vtable;
    scene->bounds = bounds;
    scene->user_data = data;
    rng_seed(&scene->rng, 42);

    return scene;
}

void gi_test_scene_destroy(Scene *scene)
{
    scene_destroy(scene);
}

void gi_test_set_ray(Scene *scene, Vec3 origin, Vec3 dir)
{
    if (!scene || !scene->user_data)
        return;
    GITestData *data = (GITestData *)scene->user_data;
    data->ray_origin = origin;
    data->ray_dir = dir;
}

VoxelVolume *gi_test_get_terrain(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((GITestData *)scene->user_data)->terrain;
}

VoxelObjectWorld *gi_test_get_objects(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((GITestData *)scene->user_data)->objects;
}

ParticleSystem *gi_test_get_particles(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((GITestData *)scene->user_data)->particles;
}

PhysicsWorld *gi_test_get_physics(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((GITestData *)scene->user_data)->physics;
}
