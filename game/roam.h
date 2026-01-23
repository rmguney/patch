#ifndef PATCH_GAME_ROAM_H
#define PATCH_GAME_ROAM_H

#include "engine/sim/scene.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include "engine/core/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int32_t num_pillars;
        float terrain_amplitude;
        float terrain_frequency;
    } RoamParams;

    typedef struct
    {
        int32_t terrain_voxels;
        int32_t pillar_count;
        int32_t particles_active;
    } RoamStats;

    typedef struct
    {
        VoxelVolume *terrain;
        VoxelObjectWorld *objects;
        ParticleSystem *particles;

        RoamParams params;
        RoamStats stats;

        Vec3 ray_origin;
        Vec3 ray_dir;
        float voxel_size;

        bool use_ortho_camera;
        bool left_was_down;
    } RoamData;

    RoamParams roam_default_params(void);
    Scene *roam_scene_create(Bounds3D bounds, float voxel_size, const RoamParams *params);
    void roam_scene_destroy(Scene *scene);

    void roam_set_ray(Scene *scene, Vec3 origin, Vec3 direction);
    bool roam_get_ortho_camera(Scene *scene);
    void roam_set_ortho_camera(Scene *scene, bool ortho);
    void roam_toggle_camera(Scene *scene);

    VoxelVolume *roam_get_terrain(Scene *scene);
    VoxelObjectWorld *roam_get_objects(Scene *scene);
    ParticleSystem *roam_get_particles(Scene *scene);
    const RoamStats *roam_get_stats(Scene *scene);

#ifdef __cplusplus
}
#endif

#endif
