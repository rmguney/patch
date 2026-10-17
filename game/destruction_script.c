#include "game/destruction_script.h"
#include "engine/core/rng.h"
#include "content/materials.h"

/* One voxel more than a click carves: the scripted run is a destruction stress test */
#define SCRIPT_DESTROY_RADIUS_VOXELS 3

bool destruction_script_step(Scene *scene, WorldSceneDestruction *out)
{
    *out = (WorldSceneDestruction){0};
    VoxelVolume *terrain = world_scene_get_terrain(scene);
    if (!terrain)
        return false;

    const Bounds3D b = scene->bounds;
    float cx = (b.min_x + b.max_x) * 0.5f;
    float cz = (b.min_z + b.max_z) * 0.5f;
    float range_x = (b.max_x - b.min_x) * 0.3f;
    float range_z = (b.max_z - b.min_z) * 0.3f;
    float px = cx + (rng_float(&scene->rng) * 2.0f - 1.0f) * range_x;
    float pz = cz + (rng_float(&scene->rng) * 2.0f - 1.0f) * range_z;

    Vec3 hit_pos;
    Vec3 hit_normal;
    float hit_dist = volume_raycast(terrain, vec3_create(px, b.max_y, pz), vec3_create(0.0f, -1.0f, 0.0f),
                                    b.max_y - b.min_y, &hit_pos, &hit_normal, NULL, MAT_FLAG_LIQUID);
    if (hit_dist < 0.0f)
        return false;

    /* Centre the carve inside the hit voxel, not on its face (see world_scene_handle_input) */
    Vec3 center = vec3_sub(hit_pos, vec3_scale(hit_normal, terrain->voxel_size * 0.5f));
    *out = world_scene_destroy_sphere(scene, center, SCRIPT_DESTROY_RADIUS_VOXELS);
    return true;
}

void destruction_script_frame(DestructionScript *script, Scene *scene, int32_t frame)
{
    if (script->interval_frames <= 0 || frame <= 0 || frame % script->interval_frames != 0)
        return;

    WorldSceneDestruction destruction;
    if (destruction_script_step(scene, &destruction))
    {
        script->destructions++;
        script->voxels_removed += destruction.voxels_removed;
    }
}
