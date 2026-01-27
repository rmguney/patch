#ifndef PATCH_GAME_ROAM_H
#define PATCH_GAME_ROAM_H

#include "engine/sim/scene.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include "engine/physics/rigidbody.h"
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
        PhysicsWorld *physics;
        void *detach_work; /* ConnectivityWorkBuffer* - opaque to avoid header dependency */

        RoamParams params;
        RoamStats stats;
        Vec3 ray_origin;
        Vec3 ray_dir;
        float voxel_size;

        bool left_was_down;
        bool pending_connectivity;
        bool detach_ready;
        double last_connectivity_time;
        Vec3 last_destroy_point;
    } RoamData;

    RoamParams roam_default_params(void);
    Scene *roam_scene_create(Bounds3D bounds, float voxel_size, const RoamParams *params);
    void roam_scene_destroy(Scene *scene);

    void roam_set_ray(Scene *scene, Vec3 origin, Vec3 direction);

    VoxelVolume *roam_get_terrain(Scene *scene);
    VoxelObjectWorld *roam_get_objects(Scene *scene);
    ParticleSystem *roam_get_particles(Scene *scene);
    PhysicsWorld *roam_get_physics(Scene *scene);
    const RoamStats *roam_get_stats(Scene *scene);

#ifdef __cplusplus
}
#endif

#endif
