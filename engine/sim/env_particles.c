#include "env_particles.h"
#include "content/materials.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct
{
    float gravity;
    float base_radius;
    float min_lifetime;
    float max_lifetime;
    float spawn_chance;
    bool emissive;
} EnvParticleParams;

/* spawn_chance: probability per emitter per heartbeat tick (0.5s period) */
static const EnvParticleParams s_params[ENV_PARTICLE_TYPE_COUNT] = {
    [ENV_PARTICLE_LEAF]    = { .gravity = -0.6f,  .base_radius = 0.04f, .min_lifetime = 6.0f,  .max_lifetime = 12.0f, .spawn_chance = 0.10f,  .emissive = false },
    [ENV_PARTICLE_DUST]    = { .gravity = -0.03f, .base_radius = 0.02f, .min_lifetime = 5.0f,  .max_lifetime = 10.0f, .spawn_chance = 0.06f,  .emissive = false },
    [ENV_PARTICLE_FIREFLY] = { .gravity = 0.0f,   .base_radius = 0.03f, .min_lifetime = 4.0f,  .max_lifetime = 10.0f, .spawn_chance = 0.05f,  .emissive = true  },
    [ENV_PARTICLE_SNOW]    = { .gravity = -1.0f,  .base_radius = 0.03f, .min_lifetime = 8.0f,  .max_lifetime = 16.0f, .spawn_chance = 0.15f,  .emissive = false },
    [ENV_PARTICLE_POLLEN]  = { .gravity = 0.08f,  .base_radius = 0.015f,.min_lifetime = 4.0f,  .max_lifetime = 8.0f,  .spawn_chance = 0.08f,  .emissive = false },
    [ENV_PARTICLE_DRIP]    = { .gravity = -4.0f,  .base_radius = 0.02f, .min_lifetime = 1.0f,  .max_lifetime = 3.0f,  .spawn_chance = 0.04f,  .emissive = false },
    [ENV_PARTICLE_STEAM]   = { .gravity = 0.5f,   .base_radius = 0.05f, .min_lifetime = 2.0f,  .max_lifetime = 5.0f,  .spawn_chance = 0.08f,  .emissive = false },
    [ENV_PARTICLE_BEE]     = { .gravity = 0.0f,   .base_radius = 0.025f,.min_lifetime = 5.0f,  .max_lifetime = 15.0f, .spawn_chance = 0.03f,  .emissive = false },
};

EnvParticleSystem *env_particles_create(uint32_t seed)
{
    EnvParticleSystem *sys = (EnvParticleSystem *)calloc(1, sizeof(EnvParticleSystem));
    if (!sys)
        return NULL;

    rng_seed(&sys->rng, seed + 333333);
    heartbeat_init(&sys->scheduler);
    sys->wind_velocity = vec3_create(0.3f, 0.0f, 0.1f);
    sys->wind_turbulence = 0.5f;

    return sys;
}

void env_particles_destroy(EnvParticleSystem *sys)
{
    free(sys);
}

static Vec3 particle_color_for_type(uint8_t type, uint8_t source_mat, RngState *rng)
{
    switch (type)
    {
    case ENV_PARTICLE_LEAF:
    {
        Vec3 base = material_get_color(source_mat);
        float variation = 0.1f;
        return vec3_create(
            clampf(base.x + rng_range_f32(rng, -variation, variation), 0.0f, 1.0f),
            clampf(base.y + rng_range_f32(rng, -variation, variation), 0.0f, 1.0f),
            clampf(base.z + rng_range_f32(rng, -variation, variation), 0.0f, 1.0f));
    }
    case ENV_PARTICLE_DUST:
        return vec3_create(0.6f, 0.55f, 0.45f);
    case ENV_PARTICLE_FIREFLY:
        return vec3_create(0.8f, 1.0f, 0.3f);
    case ENV_PARTICLE_SNOW:
        return vec3_create(0.95f, 0.95f, 1.0f);
    case ENV_PARTICLE_POLLEN:
        return vec3_create(0.9f, 0.85f, 0.3f);
    case ENV_PARTICLE_DRIP:
        return vec3_create(0.4f, 0.5f, 0.7f);
    case ENV_PARTICLE_STEAM:
        return vec3_create(0.85f, 0.85f, 0.9f);
    case ENV_PARTICLE_BEE:
        return vec3_create(0.9f, 0.75f, 0.1f);
    default:
        return vec3_create(1.0f, 1.0f, 1.0f);
    }
}

static int32_t spawn_particle(EnvParticleSystem *sys, Vec3 pos, uint8_t type, uint8_t source_mat)
{
    int32_t slot = sys->next_slot;
    EnvParticle *p = &sys->particles[slot];
    const EnvParticleParams *params = &s_params[type];

    p->position = pos;
    p->prev_position = pos;
    p->type = type;
    p->active = true;
    p->emissive = params->emissive;
    p->radius = params->base_radius * (0.8f + rng_float(&sys->rng) * 0.4f);
    p->lifetime = 0.0f;
    p->max_lifetime = lerpf(params->min_lifetime, params->max_lifetime, rng_float(&sys->rng));
    p->color = particle_color_for_type(type, source_mat, &sys->rng);

    float spread = 0.5f;
    p->velocity = vec3_create(
        rng_range_f32(&sys->rng, -spread, spread),
        params->gravity * 0.3f,
        rng_range_f32(&sys->rng, -spread, spread));

    sys->next_slot = (slot + 1) % ENV_PARTICLE_MAX;
    if (sys->count < ENV_PARTICLE_MAX)
        sys->count++;

    return slot;
}

void env_particles_update(EnvParticleSystem *sys, float dt, double time)
{
    if (!sys)
        return;

    heartbeat_maintain(&sys->scheduler, time);

    /* Spawn from emitters using a single fixed-period heartbeat */
    uint8_t beats = heartbeat_get(&sys->scheduler, 0.5f);
    if (beats > 0)
    {
        for (int32_t e = 0; e < sys->emitter_count; e++)
        {
            const EnvEmitter *emitter = &sys->emitters[e];
            const EnvParticleParams *params = &s_params[emitter->type];

            float chance = params->spawn_chance * (float)beats;
            if (rng_float(&sys->rng) < chance)
            {
                Vec3 offset = vec3_create(
                    rng_range_f32(&sys->rng, -0.3f, 0.3f),
                    rng_range_f32(&sys->rng, 0.0f, 0.3f),
                    rng_range_f32(&sys->rng, -0.3f, 0.3f));
                Vec3 pos = vec3_add(emitter->position, offset);
                spawn_particle(sys, pos, emitter->type, emitter->source_material);
            }
        }
    }

    /* Update active particles */
    int32_t active = 0;
    int32_t updated = 0;

    for (int32_t i = 0; i < sys->count && updated < ENV_PARTICLE_BUDGET_PER_TICK; i++)
    {
        EnvParticle *p = &sys->particles[i];
        if (!p->active)
            continue;

        p->lifetime += dt;
        if (p->lifetime >= p->max_lifetime)
        {
            p->active = false;
            continue;
        }

        const EnvParticleParams *params = &s_params[p->type];

        p->prev_position = p->position;
        p->velocity.y += params->gravity * dt;

        Vec3 wind = sys->wind_velocity;
        if (p->type == ENV_PARTICLE_LEAF)
        {
            float flutter = sinf((float)time * 3.0f + p->position.x * 2.0f) * 0.3f;
            wind.x += flutter;
            wind.z += cosf((float)time * 2.5f + p->position.z * 2.0f) * 0.2f;
        }
        else if (p->type == ENV_PARTICLE_FIREFLY)
        {
            p->velocity.x += sinf((float)time * 1.5f + (float)i) * 0.5f * dt;
            p->velocity.y += cosf((float)time * 2.0f + (float)i * 0.7f) * 0.3f * dt;
            p->velocity.z += cosf((float)time * 1.8f + (float)i * 1.3f) * 0.5f * dt;
        }
        else if (p->type == ENV_PARTICLE_BEE)
        {
            float angle = (float)time * 4.0f + (float)i * 1.7f;
            p->velocity.x += cosf(angle) * 1.0f * dt;
            p->velocity.z += sinf(angle) * 1.0f * dt;
        }

        p->velocity = vec3_add(p->velocity, vec3_scale(wind, dt));

        float damping = 0.98f;
        p->velocity = vec3_scale(p->velocity, damping);

        float max_vel = 3.0f;
        float vel_sq = vec3_length_sq(p->velocity);
        if (vel_sq > max_vel * max_vel)
            p->velocity = vec3_scale(p->velocity, max_vel / sqrtf(vel_sq));

        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));

        active++;
        updated++;
    }

    sys->active_count = active;
}

void env_particles_register_emitters(EnvParticleSystem *sys,
                                      const VoxelVolume *vol,
                                      float voxel_size)
{
    if (!sys || !vol)
        return;

    sys->emitter_count = 0;
    RngState scan_rng;
    rng_seed(&scan_rng, 54321);

    for (float x = vol->bounds.min_x; x < vol->bounds.max_x; x += voxel_size)
    {
        for (float z = vol->bounds.min_z; z < vol->bounds.max_z; z += voxel_size)
        {
            if (sys->emitter_count >= ENV_EMITTER_MAX)
                return;

            for (float y = vol->bounds.max_y - voxel_size; y > vol->bounds.min_y; y -= voxel_size)
            {
                Vec3 pos = vec3_create(x, y, z);
                uint8_t mat = volume_get_at(vol, pos);
                if (mat == 0)
                    continue;

                Vec3 above = vec3_create(x, y + voxel_size, z);
                if (volume_get_at(vol, above) != 0)
                    break;

                uint8_t emitter_type = 255;
                uint8_t source_mat = mat;

                if (mat >= MAT_OAK_LEAF && mat <= MAT_AUTUMN_LEAF)
                {
                    if (rng_float(&scan_rng) < 0.08f)
                        emitter_type = ENV_PARTICLE_LEAF;
                }
                else if (mat == MAT_GRASS)
                {
                    if (rng_float(&scan_rng) < 0.015f)
                    {
                        emitter_type = rng_float(&scan_rng) < 0.2f
                            ? ENV_PARTICLE_FIREFLY
                            : ENV_PARTICLE_DUST;
                    }
                }
                else if (mat >= MAT_FLOWER_RED && mat <= MAT_FLOWER_YELLOW)
                {
                    if (rng_float(&scan_rng) < 0.15f)
                        emitter_type = ENV_PARTICLE_POLLEN;
                }

                if (emitter_type != 255)
                {
                    EnvEmitter *e = &sys->emitters[sys->emitter_count++];
                    e->position = above;
                    e->type = emitter_type;
                    e->source_material = source_mat;
                }

                break;
            }
        }
    }
}

void env_particles_set_wind(EnvParticleSystem *sys, Vec3 direction, float strength)
{
    if (!sys)
        return;
    sys->wind_velocity = vec3_scale(direction, strength);
}

int32_t env_particles_get_active(const EnvParticleSystem *sys)
{
    return sys ? sys->active_count : 0;
}
