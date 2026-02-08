#ifndef PATCH_SIM_ENV_PARTICLES_H
#define PATCH_SIM_ENV_PARTICLES_H

/*
 * Environmental particle system for ambient effects.
 * Separate from destruction particles: no collision, no settlement,
 * lightweight lifetime + wind physics only.
 *
 * Ported from Veloren's BlockParticles / maintain_block_particles()
 * (particle.rs:2904-3100).
 */

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/sim/heartbeat.h"
#include "engine/voxel/volume.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ENV_PARTICLE_MAX 16384
#define ENV_EMITTER_MAX 4096
#define ENV_PARTICLE_BUDGET_PER_TICK 8192

    typedef enum
    {
        ENV_PARTICLE_LEAF,
        ENV_PARTICLE_DUST,
        ENV_PARTICLE_FIREFLY,
        ENV_PARTICLE_SNOW,
        ENV_PARTICLE_POLLEN,
        ENV_PARTICLE_DRIP,
        ENV_PARTICLE_STEAM,
        ENV_PARTICLE_BEE,
        ENV_PARTICLE_TYPE_COUNT
    } EnvParticleType;

    typedef struct
    {
        Vec3 position;
        Vec3 prev_position;
        Vec3 velocity;
        Vec3 color;
        float radius;
        float lifetime;
        float max_lifetime;
        uint8_t type;
        bool active;
        bool emissive;
    } EnvParticle;

    typedef struct
    {
        Vec3 position;
        uint8_t type;
        uint8_t source_material;
    } EnvEmitter;

    typedef struct
    {
        EnvParticle particles[ENV_PARTICLE_MAX];
        int32_t count;
        int32_t next_slot;
        int32_t active_count;

        EnvEmitter emitters[ENV_EMITTER_MAX];
        int32_t emitter_count;

        Vec3 wind_velocity;
        float wind_turbulence;
        HeartbeatScheduler scheduler;
        RngState rng;
    } EnvParticleSystem;

    EnvParticleSystem *env_particles_create(uint32_t seed);
    void env_particles_destroy(EnvParticleSystem *sys);
    void env_particles_update(EnvParticleSystem *sys, float dt, double time);
    void env_particles_register_emitters(EnvParticleSystem *sys,
                                          const VoxelVolume *vol,
                                          float voxel_size);
    void env_particles_set_wind(EnvParticleSystem *sys, Vec3 direction, float strength);
    int32_t env_particles_get_active(const EnvParticleSystem *sys);

#ifdef __cplusplus
}
#endif

#endif
