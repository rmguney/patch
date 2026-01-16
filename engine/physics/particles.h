#ifndef PATCH_CORE_PARTICLES_H
#define PATCH_CORE_PARTICLES_H

/*
 * Visual-only particle system for debris and effects.
 * Lightweight: simple gravity, floor bounce, particle-particle collision.
 * NOT integrated with rigid body physics - purely cosmetic.
 */

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/core/spatial_hash.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PARTICLE_MAX_COUNT 65536
#define PARTICLE_MAX_UPDATES_PER_TICK 16384
#define PARTICLE_YOUNG_AGE_THRESHOLD 1.0f
#define PARTICLE_SETTLE_VELOCITY 0.15f
/* No PARTICLE_LIFETIME_MAX - particles are removed via circular buffer when spawning at capacity */

    typedef struct
    {
        Vec3 position;
        Vec3 prev_position;
        Vec3 velocity;
        Vec3 rotation;
        Vec3 prev_rotation;
        Vec3 angular_velocity;
        Vec3 color;
        float radius;
        float lifetime;
        bool active;
        bool settled;
    } Particle;

    typedef struct
    {
        Particle particles[PARTICLE_MAX_COUNT];
        int32_t count;
        int32_t next_slot;

        Bounds3D bounds;
        Vec3 gravity;

        float damping;
        float restitution;
        float floor_friction;

        bool enable_particle_collision;
        SpatialHashGrid collision_grid;

        int32_t update_cursor;  /* Round-robin cursor for budgeted updates */
        int32_t active_count;   /* Tracked count to avoid O(n) scans */
    } ParticleSystem;

    ParticleSystem *particle_system_create(Bounds3D bounds);
    void particle_system_destroy(ParticleSystem *sys);
    void particle_system_update(ParticleSystem *sys, float dt);
    void particle_system_clear(ParticleSystem *sys);

    int32_t particle_system_spawn_explosion(ParticleSystem *sys, RngState *rng, Vec3 center, float radius,
                                            Vec3 color, int32_t count, float force);

    int32_t particle_system_spawn_at_impact(ParticleSystem *sys, RngState *rng, Vec3 impact_point, Vec3 ball_center,
                                            float ball_radius, Vec3 color, int32_t count, float force);

    int32_t particle_system_get_settled(ParticleSystem *sys, Particle *out_settled, int32_t max_count);
    void particle_system_remove_settled(ParticleSystem *sys);

    bool particle_system_pickup_nearest(ParticleSystem *sys, Vec3 position, float max_dist, Vec3 *out_color);

    int32_t particle_system_add(ParticleSystem *sys, RngState *rng, Vec3 position, Vec3 velocity, Vec3 color, float radius);
    Particle *particle_system_add_slot(ParticleSystem *sys);

#ifdef __cplusplus
}
#endif

#endif
