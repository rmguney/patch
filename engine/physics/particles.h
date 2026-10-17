#ifndef PATCH_PHYSICS_PARTICLES_H
#define PATCH_PHYSICS_PARTICLES_H

/*
 * Visual-only particle system for debris and effects.
 * Lightweight: gravity, terrain and object collision, particle-particle collision.
 * Particles never push rigid bodies; they are purely cosmetic.
 */

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/spatial_hash.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Live particles are packed in [0, count): the moving ones first, then the settled
 * ones, so a tick costs the moving particles plus a budget of settled checks however
 * much debris lies around. Settled debris stays PARTICLE_SETTLED_LIFETIME seconds;
 * at capacity a new particle replaces a settled one (the earliest settled roughly), or
 * when every particle is moving, moving ones in turn. */
#define PARTICLE_MAX_COUNT 65536
#define PARTICLE_MAX_UPDATES_PER_TICK 16384 /* Budget for moving particles past the young age */
#define PARTICLE_MAX_SETTLED_CHECKS_PER_TICK 4096 /* Support (wake) and lifetime checks */
#define PARTICLE_MAX_COLLISION_PAIRS 8192
#define PARTICLE_YOUNG_AGE_THRESHOLD 1.0f /* Younger particles update every tick */
#define PARTICLE_SETTLE_VELOCITY 0.15f
#define PARTICLE_SETTLED_LIFETIME 30.0f

    typedef struct
    {
        Vec3 position;
        Vec3 prev_position; /* For render interpolation */
        Vec3 velocity;
        Vec3 color; /* Linear, ready for the G-buffer (converted on add) */
        float radius;
        float lifetime;    /* Seconds spent moving */
        float moved;       /* Distance moved over the last tick, contacts included */
        float expire_time; /* Settled: ParticleSystem.time at which it is retired */
        bool active;       /* Every particle in [0, count) is (read by the renderer) */
        bool settled;
    } Particle;

    typedef struct
    {
        Particle particles[PARTICLE_MAX_COUNT];
        int32_t count;       /* Live particles, [0, count) */
        int32_t awake_count; /* [0, awake_count) are moving, [awake_count, count) settled */
        int32_t replace_cursor; /* Moving particle replaced next when all of them move */
        double time;         /* Seconds simulated */

        Vec3 gravity;
        float damping;
        float restitution;
        float floor_friction;

        SpatialHashGrid collision_grid;

        int32_t update_cursor; /* Round-robin cursor for budgeted updates of moving particles */
        int32_t settle_cursor; /* Round-robin cursor for settled checks */
    } ParticleSystem;

    ParticleSystem *particle_system_create(Bounds3D bounds);
    void particle_system_destroy(ParticleSystem *sys);
    void particle_system_update(ParticleSystem *sys, float dt,
                                const VoxelVolume *terrain,
                                const VoxelObjectWorld *objects);

    /* color is sRGB as authored (material_get_color); stored linear like the voxel palette */
    void particle_system_add(ParticleSystem *sys, Vec3 position, Vec3 velocity, Vec3 color, float radius);

#ifdef __cplusplus
}
#endif

#endif
