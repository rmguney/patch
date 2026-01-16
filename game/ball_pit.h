#ifndef PATCH_SCENES_BALL_PIT_H
#define PATCH_SCENES_BALL_PIT_H

#include "engine/sim/scene.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int32_t initial_spawns;
        float spawn_interval;
        int32_t spawn_batch;
        int32_t max_spawns;
    } BallPitParams;

    typedef struct
    {
        float tick_time_us;
        int32_t spawn_count;
        int32_t tick_count;
    } BallPitStats;

    typedef struct
    {
        Vec3 ray_origin;
        Vec3 ray_dir;
        float spawn_timer;
        float voxel_size;

        BallPitParams params;
        BallPitStats stats;

        /* Scene systems */
        VoxelVolume *terrain;
        VoxelObjectWorld *objects;
        ParticleSystem *particles;

        /* Terrain detachment (floating islands -> voxel objects) */
        ConnectivityWorkBuffer detach_work;
        bool detach_ready;
        bool pending_connectivity;  /* Run connectivity on next frame when not destroying */
        double last_connectivity_time; /* Time of last connectivity analysis (for throttling) */
    } BallPitData;

    BallPitParams ball_pit_default_params(void);
    Scene *ball_pit_scene_create(Bounds3D bounds, float voxel_size, const BallPitParams *params);
    void ball_pit_scene_destroy(Scene *scene);

    void ball_pit_set_ray(Scene *scene, Vec3 origin, Vec3 dir);

    /* Accessors for renderer */
    VoxelVolume *ball_pit_get_terrain(Scene *scene);
    VoxelObjectWorld *ball_pit_get_objects(Scene *scene);
    ParticleSystem *ball_pit_get_particles(Scene *scene);

#ifdef __cplusplus
}
#endif

#endif
