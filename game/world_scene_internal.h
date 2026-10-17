#ifndef PATCH_GAME_WORLD_SCENE_INTERNAL_H
#define PATCH_GAME_WORLD_SCENE_INTERNAL_H

/* World scene scene state, private to world_scene.c and its white-box tests: everything else,
 * the app included, drives the scene through world_scene.h */

#include "game/world_scene.h"
#include "engine/voxel/connectivity.h"
#include "engine/physics/rigidbody.h"
#include "game/day_night.h"
#include "game/weather.h"
#include "game/terrain_gen.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        Vec3 ray_origin;
        Vec3 ray_dir;
        float voxel_size;

        WorldSceneParams params;
        WorldSceneStats stats;

        /* Scene systems */
        VoxelVolume *terrain;
        VoxelObjectWorld *objects;
        ParticleSystem *particles;
        EnvParticleSystem *env_particles;
        PhysicsWorld *physics;

        /* Terrain detachment (floating islands -> voxel objects) */
        ConnectivityWorkBuffer detach_work;
        bool detach_ready;
        bool pending_connectivity;  /* Run connectivity in the next tick with the left button up */
        Vec3 last_destroy_point;    /* Last terrain destruction center (for detach impulse) */
        /* Loading: the loader thread running the init stages, and the job system they
         * generate with; both live only while loading */
        BackgroundLoad *loader;
        JobSystem *jobs;
        bool loaded;
        /* Column heights and biomes shared by the world generation stages, freed after them */
        TerrainColumns columns;
        bool right_was_down;
        /* Left button as the ticks see it: held now, and pressed at all since the last tick */
        bool left_held;
        bool left_pressed;

        /* Day/night + weather */
        DayNightCycle day_night;
        WeatherSystem weather;
        SceneLighting lighting;
    } WorldSceneData;

    /* Runs init stage `stage` (0 .. WORLD_SCENE_INIT_STAGES - 1) of the scene passed as ctx:
     * the stages of scene_init and world_scene_init_step, one at a time for white-box tests */
    void world_scene_run_init_stage(void *ctx, int32_t stage);

#ifdef __cplusplus
}
#endif

#endif
