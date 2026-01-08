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

/*
 * Predefined scene IDs matching registration order in scenes.c.
 */
#define SCENE_ID_BALL_PIT 0

#ifdef __cplusplus
}
#endif

#endif
