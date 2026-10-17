#ifndef PATCH_GAME_WORLD_SCENE_H
#define PATCH_GAME_WORLD_SCENE_H

#include "engine/sim/scene.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include "engine/sim/env_particles.h"
#include "engine/sim/loading.h"
#include "content/scenes.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int32_t num_pillars;
        float terrain_amplitude;
        float terrain_frequency;
        float tree_density; /* Scales every biome's tree chance */
    } WorldSceneParams;

    typedef struct
    {
        int32_t spawn_count;
        int32_t tick_count;
    } WorldSceneStats;

    /* What one terrain destruction did */
    typedef struct
    {
        int32_t voxels_removed;
        int32_t debris_spawned; /* particles burst from the removed voxels (capped) */
    } WorldSceneDestruction;

    WorldSceneParams world_scene_default_params(void);
    Scene *world_scene_create(Bounds3D bounds, float voxel_size, const WorldSceneParams *params);

    void world_scene_set_ray(Scene *scene, Vec3 origin, Vec3 dir);

#define WORLD_SCENE_INIT_STAGES 9

    /* Stepped init: returns true when all stages complete */
    bool world_scene_init_step(Scene *scene, LoadingState *state);

    /* Carves a sphere of radius_voxels (whole voxels) around a world point out of the
     * terrain, as a left click on it does: breakable voxels are removed and burst as
     * debris (one scene RNG draw each), bodies nearby are woken, and the floating-island
     * check is queued. It runs in the next tick with the left button up and launches what
     * came loose. */
    WorldSceneDestruction world_scene_destroy_sphere(Scene *scene, Vec3 center, int32_t radius_voxels);

    /* Accessors for the app: renderer feed, debug overlay */
    VoxelVolume *world_scene_get_terrain(Scene *scene);
    VoxelObjectWorld *world_scene_get_objects(Scene *scene);
    ParticleSystem *world_scene_get_particles(Scene *scene);
    EnvParticleSystem *world_scene_get_env_particles(Scene *scene);
    const SceneLighting *world_scene_get_lighting(Scene *scene);
    const WorldSceneStats *world_scene_get_stats(Scene *scene);
    void world_scene_set_time_of_day(Scene *scene, float time);
    void world_scene_set_weather(Scene *scene, int weather_state);

#ifdef __cplusplus
}
#endif

#endif
