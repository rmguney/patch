#ifndef PATCH_CONTENT_SCENES_H
#define PATCH_CONTENT_SCENES_H

#include "engine/core/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum scenes in registration table */
#define SCENE_MAX_COUNT 16

    typedef enum
    {
        SCENE_TYPE_BALL_PIT,
        SCENE_TYPE_ROAM,
        SCENE_TYPE_COUNT
    } SceneType;

    /*
     * SceneDescriptor: immutable definition of a scene's mission scope.
     * This is the single source of truth for scene bounds and voxel resolution.
     */
    typedef struct
    {
        const char *name;      /* Display name */
        SceneType type;        /* Scene type for factory */
        Bounds3D bounds;       /* World-space bounds */
        int32_t chunks_x;      /* Chunk coverage X */
        int32_t chunks_y;      /* Chunk coverage Y */
        int32_t chunks_z;      /* Chunk coverage Z */
        float voxel_size;      /* Size of each voxel in world units */
        uint32_t rng_seed;     /* Initial RNG seed */
        int32_t max_entities;  /* Maximum entity count */
        int32_t max_particles; /* Maximum particle count */
    } SceneDescriptor;

    /* Global scene registration table */
    extern const SceneDescriptor g_scenes[SCENE_MAX_COUNT];
    extern const int32_t g_scene_count;

    /* Lookup scene by type */
    static inline const SceneDescriptor *scene_get_descriptor(SceneType type)
    {
        if (type >= SCENE_TYPE_COUNT)
            return &g_scenes[0];
        return &g_scenes[type];
    }

    /* Compute bounds from chunks (single source of truth for terrain dimensions) */
    static inline Bounds3D scene_compute_terrain_bounds(const SceneDescriptor *desc)
    {
        float chunk_world_size = 32.0f * desc->voxel_size; /* CHUNK_SIZE = 32 */
        float half_x = desc->chunks_x * chunk_world_size * 0.5f;
        float half_z = desc->chunks_z * chunk_world_size * 0.5f;
        float height = desc->chunks_y * chunk_world_size;
        Bounds3D b;
        b.min_x = -half_x;
        b.max_x = half_x;
        b.min_y = 0.0f;
        b.max_y = height;
        b.min_z = -half_z;
        b.max_z = half_z;
        return b;
    }

/*
 * Predefined scene IDs matching registration order in scenes.c.
 */
#define SCENE_ID_BALL_PIT 0
#define SCENE_ID_ROAM 1

#ifdef __cplusplus
}
#endif

#endif
