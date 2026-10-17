#ifndef PATCH_SIM_ENV_PARTICLES_H
#define PATCH_SIM_ENV_PARTICLES_H

/*
 * Environmental particle system for ambient effects and weather.
 * Separate from destruction particles: no collision response, no settlement. Each type
 * drifts with the wind at its own settling speed; rain and snow end where they reach a
 * voxel.
 *
 * Emitters ported from Veloren's BlockParticles / maintain_block_particles()
 * (particle.rs:2904-3100).
 */

#include "engine/core/types.h"
#include "engine/core/jobs.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ENV_PARTICLE_MAX 16384
#define ENV_EMITTER_MAX 4096
/* Every emitter rolls for a particle once per period (seconds) */
#define ENV_EMITTER_PERIOD 0.5f

    typedef enum
    {
        ENV_PARTICLE_LEAF,
        ENV_PARTICLE_DUST,
        ENV_PARTICLE_FIREFLY,
        ENV_PARTICLE_SNOW,
        ENV_PARTICLE_POLLEN,
        ENV_PARTICLE_RAIN,
        ENV_PARTICLE_STEAM,
        ENV_PARTICLE_BEE,
        ENV_PARTICLE_TYPE_COUNT
    } EnvParticleType;

    typedef struct
    {
        Vec3 position;
        Vec3 prev_position;
        Vec3 velocity;
        Vec3 color; /* Linear, ready for the G-buffer */
        float radius;
        float lifetime;
        float max_lifetime;
        uint8_t type;
        bool active;
        bool emissive;
    } EnvParticle;

    typedef struct
    {
        Vec3 position;       /* On top of the source voxel */
        int32_t gx, gy, gz;  /* Source voxel */
        uint8_t type;
        uint8_t source_material;
    } EnvEmitter;

    /* A voxel of material open to the air above emits particles of type with probability
     * density. A material with several rules splits one draw between them, in order. */
    typedef struct
    {
        uint8_t material;
        uint8_t type; /* EnvParticleType */
        float density;
    } EnvEmitterRule;

    extern const EnvEmitterRule g_env_emitter_rules[];
    extern const int32_t g_env_emitter_rule_count;

    typedef struct
    {
        EnvParticle particles[ENV_PARTICLE_MAX];
        int32_t count;
        int32_t next_slot;
        int32_t active_count;

        EnvEmitter emitters[ENV_EMITTER_MAX];
        int32_t emitter_count;
        float emitter_clock; /* Seconds since the emitters last rolled */

        Vec3 wind_velocity;
        RngState rng; /* Emitter rolls and their particles */

        uint32_t seed; /* World seed: places the emitters */
    } EnvParticleSystem;

    EnvParticleSystem *env_particles_create(uint32_t seed);
    void env_particles_destroy(EnvParticleSystem *sys);
    /* Advances the particles by dt and lets the emitters spawn. With a volume, rain and
     * snow end in the first voxel they enter, and an emitter whose source voxel is gone or
     * covered stops for good. daylight (0 night .. 1 day) brings out fireflies at night
     * and bees by day; time drives the flutter of leaves and snow. */
    void env_particles_update(EnvParticleSystem *sys, const VoxelVolume *vol, float daylight, float dt,
                              double time);
    /* Emitters over the open voxels of the whole volume (g_env_emitter_rules): each
     * column's top voxel and, under a canopy (materials whose particles are leaves), the
     * ground beneath it. When a particle type has more candidates than its share of
     * ENV_EMITTER_MAX, an even random sample of them is kept, so emitters spread over the
     * whole world; the choice depends only on the seed and the volume, not on the thread
     * count. */
    void env_particles_register_emitters(EnvParticleSystem *sys,
                                          const VoxelVolume *vol,
                                          float voxel_size,
                                          JobSystem *jobs);
    void env_particles_set_wind(EnvParticleSystem *sys, Vec3 direction, float strength);
    /* Velocity a particle of type settles at in the current wind */
    Vec3 env_particles_steady_velocity(const EnvParticleSystem *sys, uint8_t type);
    /* Adds one particle (the weather's rain and snow); its size, lifetime and colour vary
     * by draws from rng */
    void env_particles_spawn(EnvParticleSystem *sys, uint8_t type, Vec3 position, Vec3 velocity, RngState *rng);

#ifdef __cplusplus
}
#endif

#endif
