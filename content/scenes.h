#ifndef PATCH_CONTENT_SCENES_H
#define PATCH_CONTENT_SCENES_H

#include "engine/core/types.h"
#include "engine/core/math.h"
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

    typedef struct
    {
        Vec3 sun_direction;
        Vec3 sun_color;
        float sun_strength;
        Vec3 fill_direction;
        Vec3 fill_color;
        float fill_strength;
        Vec3 back_direction;
        Vec3 back_color;
        float back_strength;
        Vec3 sky_ambient_color;
        Vec3 ground_ambient_color;
        float ambient_strength;
        Vec3 sky_color;
        float exposure;
        float wrap_bias;
        float rim_strength;
        float emissive_multiplier;
        float shadow_max_dist;
    } SceneLighting;

    /* Pre-normalized directions: normalize(-0.6,0.9,0.35), normalize(0.45,0.5,-0.65), normalize(-0.35,0.28,0.9) */
#define SCENE_LIGHTING_DEFAULT {                                            \
        .sun_direction  = {-0.52775940f, 0.79163909f, 0.30785965f},        \
        .sun_color      = {1.0f, 0.98f, 0.95f},                            \
        .sun_strength   = 1.0f,                                             \
        .fill_direction = {0.48107024f, 0.53452248f, -0.69487923f},         \
        .fill_color     = {0.7f, 0.8f, 1.0f},                              \
        .fill_strength  = 0.25f,                                            \
        .back_direction = {-0.34810796f, 0.27848636f, 0.89513475f},         \
        .back_color     = {0.95f, 0.9f, 0.85f},                            \
        .back_strength  = 0.12f,                                            \
        .sky_ambient_color   = {0.55f, 0.68f, 0.95f},                      \
        .ground_ambient_color = {0.38f, 0.32f, 0.28f},                     \
        .ambient_strength = 0.32f,                                          \
        .sky_color      = {0.75f, 0.80f, 0.95f},                           \
        .exposure       = 1.0f,                                             \
        .wrap_bias      = 0.4f,                                             \
        .rim_strength   = 0.10f,                                            \
        .emissive_multiplier = 2.0f,                                        \
        .shadow_max_dist = 80.0f                                            \
    }

    static inline SceneLighting scene_lighting_default(void)
    {
        SceneLighting l = SCENE_LIGHTING_DEFAULT;
        return l;
    }

    /*
     * SceneDescriptor: immutable definition of a scene's mission scope.
     * This is the single source of truth for scene bounds and voxel resolution.
     */
    typedef struct
    {
        const char *name;           /* Display name */
        SceneType type;             /* Scene type for factory */
        Bounds3D bounds;            /* World-space bounds */
        int32_t chunks_x;           /* Chunk coverage X */
        int32_t chunks_y;           /* Chunk coverage Y */
        int32_t chunks_z;           /* Chunk coverage Z */
        float voxel_size;           /* Size of each voxel in world units */
        uint32_t rng_seed;          /* Initial RNG seed */
        int32_t max_entities;       /* Maximum entity count */
        int32_t max_particles;      /* Maximum particle count */
        SceneLighting lighting;     /* Per-scene lighting parameters */
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

#ifdef __cplusplus
}
#endif

#endif
