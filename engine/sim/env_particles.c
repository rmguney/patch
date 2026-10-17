#include "env_particles.h"
#include "content/materials.h"
#include "engine/voxel/chunk.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* When a type's particles are about */
typedef enum
{
    ENV_ACTIVE_ALWAYS,
    ENV_ACTIVE_DAY,
    ENV_ACTIVE_NIGHT,
} EnvActive;

typedef struct
{
    float settle_speed;  /* Vertical speed against the air it settles at: below 0 falls, above 0 rises */
    float drag;          /* Rate (1/s) at which the velocity closes in on the wind plus settle_speed */
    float max_speed;
    float base_radius;
    float min_lifetime;
    float max_lifetime;
    float spawn_chance;  /* Per emitter roll; 0 for the weather's types */
    uint8_t active;      /* EnvActive */
    bool emissive;
    bool ends_on_contact; /* Ends in the first voxel it enters */
} EnvParticleParams;

/* The ambient types settle at what their old fall (or rise) per tick and 0.98 damping per
 * tick came to. Rain falls at a drop's terminal speed and snow at a flake's; their
 * lifetimes only bound a fall that meets no ground (the weather spawns them some way
 * above the viewer). */
static const EnvParticleParams s_params[ENV_PARTICLE_TYPE_COUNT] = {
    [ENV_PARTICLE_LEAF]    = { .settle_speed = -0.5f,   .drag = 1.2f, .max_speed = 3.0f,  .base_radius = 0.04f,  .min_lifetime = 6.0f,  .max_lifetime = 12.0f, .spawn_chance = 0.10f },
    [ENV_PARTICLE_DUST]    = { .settle_speed = -0.025f, .drag = 1.2f, .max_speed = 3.0f,  .base_radius = 0.02f,  .min_lifetime = 5.0f,  .max_lifetime = 10.0f, .spawn_chance = 0.06f },
    [ENV_PARTICLE_FIREFLY] = { .settle_speed = 0.0f,    .drag = 1.2f, .max_speed = 3.0f,  .base_radius = 0.03f,  .min_lifetime = 4.0f,  .max_lifetime = 10.0f, .spawn_chance = 0.05f, .active = ENV_ACTIVE_NIGHT, .emissive = true },
    [ENV_PARTICLE_SNOW]    = { .settle_speed = -1.0f,   .drag = 1.5f, .max_speed = 4.0f,  .base_radius = 0.03f,  .min_lifetime = 16.0f, .max_lifetime = 24.0f, .ends_on_contact = true },
    [ENV_PARTICLE_POLLEN]  = { .settle_speed = 0.065f,  .drag = 1.2f, .max_speed = 3.0f,  .base_radius = 0.015f, .min_lifetime = 4.0f,  .max_lifetime = 8.0f,  .spawn_chance = 0.08f },
    [ENV_PARTICLE_RAIN]    = { .settle_speed = -8.0f,   .drag = 2.0f, .max_speed = 12.0f, .base_radius = 0.02f,  .min_lifetime = 2.5f,  .max_lifetime = 4.0f,  .ends_on_contact = true },
    [ENV_PARTICLE_STEAM]   = { .settle_speed = 0.4f,    .drag = 1.2f, .max_speed = 3.0f,  .base_radius = 0.05f,  .min_lifetime = 2.0f,  .max_lifetime = 5.0f,  .spawn_chance = 0.08f },
    [ENV_PARTICLE_BEE]     = { .settle_speed = 0.0f,    .drag = 1.2f, .max_speed = 3.0f,  .base_radius = 0.025f, .min_lifetime = 5.0f,  .max_lifetime = 15.0f, .spawn_chance = 0.03f, .active = ENV_ACTIVE_DAY },
};

EnvParticleSystem *env_particles_create(uint32_t seed)
{
    EnvParticleSystem *sys = (EnvParticleSystem *)calloc(1, sizeof(EnvParticleSystem));
    if (!sys)
        return NULL;

    rng_seed(&sys->rng, seed + 333333);
    sys->seed = seed;
    sys->wind_velocity = vec3_create(0.3f, 0.0f, 0.1f);

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
    case ENV_PARTICLE_RAIN:
        return vec3_create(0.55f, 0.65f, 0.8f);
    case ENV_PARTICLE_STEAM:
        return vec3_create(0.85f, 0.85f, 0.9f);
    case ENV_PARTICLE_BEE:
        return vec3_create(0.9f, 0.75f, 0.1f);
    default:
        return vec3_create(1.0f, 1.0f, 1.0f);
    }
}

static void spawn_particle(EnvParticleSystem *sys, Vec3 pos, Vec3 velocity, uint8_t type, uint8_t source_mat,
                           RngState *rng)
{
    int32_t slot = sys->next_slot;
    EnvParticle *p = &sys->particles[slot];
    const EnvParticleParams *params = &s_params[type];

    p->position = pos;
    p->prev_position = pos;
    p->velocity = velocity;
    p->type = type;
    p->active = true;
    p->emissive = params->emissive;
    p->radius = params->base_radius * (0.8f + rng_float(rng) * 0.4f);
    p->lifetime = 0.0f;
    p->max_lifetime = lerpf(params->min_lifetime, params->max_lifetime, rng_float(rng));
    /* Authored in sRGB like the materials; the G-buffer wants linear */
    p->color = vec3_srgb_to_linear(particle_color_for_type(type, source_mat, rng));

    sys->next_slot = (slot + 1) % ENV_PARTICLE_MAX;
    if (sys->count < ENV_PARTICLE_MAX)
        sys->count++;
}

void env_particles_spawn(EnvParticleSystem *sys, uint8_t type, Vec3 position, Vec3 velocity, RngState *rng)
{
    if (!sys || type >= ENV_PARTICLE_TYPE_COUNT)
        return;
    spawn_particle(sys, position, velocity, type, MAT_AIR, rng);
}

Vec3 env_particles_steady_velocity(const EnvParticleSystem *sys, uint8_t type)
{
    return vec3_add(sys->wind_velocity, vec3_create(0.0f, s_params[type].settle_speed, 0.0f));
}

static uint8_t voxel_material(const VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz)
{
    return vol->chunks[volume_voxel_chunk_index(vol, gx, gy, gz)]
        .voxels[chunk_voxel_index(gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK, gz & CHUNK_SIZE_MASK)]
        .material;
}

/* The emitter's source voxel still holds its material with nothing on top of it */
static bool emitter_source_intact(const VoxelVolume *vol, const EnvEmitter *e)
{
    if (voxel_material(vol, e->gx, e->gy, e->gz) != e->source_material)
        return false;
    return e->gy + 1 >= vol->chunks_y * CHUNK_SIZE || voxel_material(vol, e->gx, e->gy + 1, e->gz) == MATERIAL_EMPTY;
}

/* One roll per emitter per elapsed period. An emitter whose source is gone (dug out,
 * carried off, snowed over) is dropped when it next fires: the check costs a voxel read
 * on a spawn only, and depends on nothing but the volume. */
static void spawn_from_emitters(EnvParticleSystem *sys, const VoxelVolume *vol, float daylight, float dt)
{
    sys->emitter_clock += dt;
    int32_t rolls = 0;
    while (sys->emitter_clock >= ENV_EMITTER_PERIOD)
    {
        sys->emitter_clock -= ENV_EMITTER_PERIOD;
        rolls++;
    }
    if (rolls == 0)
        return;

    float chance[ENV_PARTICLE_TYPE_COUNT];
    for (int32_t t = 0; t < ENV_PARTICLE_TYPE_COUNT; t++)
    {
        const EnvParticleParams *params = &s_params[t];
        float activity = params->active == ENV_ACTIVE_DAY ? daylight
                         : params->active == ENV_ACTIVE_NIGHT ? 1.0f - daylight
                                                              : 1.0f;
        chance[t] = params->spawn_chance * activity * (float)rolls;
    }

    for (int32_t e = 0; e < sys->emitter_count;)
    {
        EnvEmitter *emitter = &sys->emitters[e];
        if (rng_float(&sys->rng) >= chance[emitter->type])
        {
            e++;
            continue;
        }
        if (vol && !emitter_source_intact(vol, emitter))
        {
            *emitter = sys->emitters[--sys->emitter_count];
            continue;
        }

        const EnvParticleParams *params = &s_params[emitter->type];
        Vec3 offset = vec3_create(
            rng_range_f32(&sys->rng, -0.3f, 0.3f),
            rng_range_f32(&sys->rng, 0.0f, 0.3f),
            rng_range_f32(&sys->rng, -0.3f, 0.3f));
        const float spread = 0.5f;
        Vec3 velocity = vec3_create(
            rng_range_f32(&sys->rng, -spread, spread),
            params->settle_speed * 0.4f,
            rng_range_f32(&sys->rng, -spread, spread));
        spawn_particle(sys, vec3_add(emitter->position, offset), velocity, emitter->type, emitter->source_material,
                       &sys->rng);
        e++;
    }
}

void env_particles_update(EnvParticleSystem *sys, const VoxelVolume *vol, float daylight, float dt, double time)
{
    if (!sys)
        return;

    spawn_from_emitters(sys, vol, daylight, dt);

    /* Share of the gap to the steady velocity each type closes this step */
    float approach[ENV_PARTICLE_TYPE_COUNT];
    for (int32_t t = 0; t < ENV_PARTICLE_TYPE_COUNT; t++)
        approach[t] = 1.0f - expf(-s_params[t].drag * dt);

    /* Update every live particle; the pool size bounds the cost */
    int32_t active = 0;
    for (int32_t i = 0; i < sys->count; i++)
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

        Vec3 air = sys->wind_velocity;
        if (p->type == ENV_PARTICLE_LEAF)
        {
            air.x += sinf((float)time * 3.0f + p->position.x * 2.0f) * 0.3f;
            air.z += cosf((float)time * 2.5f + p->position.z * 2.0f) * 0.2f;
        }
        else if (p->type == ENV_PARTICLE_SNOW)
        {
            air.x += sinf((float)time * 1.7f + p->position.z * 1.3f) * 0.25f;
            air.z += cosf((float)time * 1.3f + p->position.x * 1.3f) * 0.25f;
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

        Vec3 steady = vec3_add(air, vec3_create(0.0f, params->settle_speed, 0.0f));
        p->velocity = vec3_add(p->velocity, vec3_scale(vec3_sub(steady, p->velocity), approach[p->type]));

        float vel_sq = vec3_length_sq(p->velocity);
        if (vel_sq > params->max_speed * params->max_speed)
            p->velocity = vec3_scale(p->velocity, params->max_speed / sqrtf(vel_sq));

        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));

        if (vol && params->ends_on_contact && volume_get_at(vol, p->position) != MATERIAL_EMPTY)
        {
            p->active = false;
            continue;
        }

        active++;
    }

    sys->active_count = active;
}

/* Ambient particles of the open voxels. Snow and rain come from the weather alone. */
const EnvEmitterRule g_env_emitter_rules[] = {
    {MAT_OAK_LEAF, ENV_PARTICLE_LEAF, 0.08f},
    {MAT_PINE_LEAF, ENV_PARTICLE_LEAF, 0.08f},
    {MAT_BIRCH_LEAF, ENV_PARTICLE_LEAF, 0.08f},
    {MAT_JUNGLE_LEAF, ENV_PARTICLE_LEAF, 0.08f},
    {MAT_CHERRY_LEAF, ENV_PARTICLE_LEAF, 0.08f},
    {MAT_AUTUMN_LEAF, ENV_PARTICLE_LEAF, 0.08f},
    /* Meadows: motes by day, fireflies by night */
    {MAT_GRASS, ENV_PARTICLE_DUST, 0.012f},
    {MAT_GRASS, ENV_PARTICLE_FIREFLY, 0.004f},
    /* Humid grounds of jungle, forest and swamp */
    {MAT_JUNGLE_GRASS, ENV_PARTICLE_FIREFLY, 0.01f},
    {MAT_MOSS, ENV_PARTICLE_FIREFLY, 0.008f},
    {MAT_MUD, ENV_PARTICLE_FIREFLY, 0.006f},
    {MAT_GLOWING_MUSHROOM, ENV_PARTICLE_FIREFLY, 0.2f},
    /* Dry and bare grounds blow dust */
    {MAT_DRY_GRASS, ENV_PARTICLE_DUST, 0.02f},
    {MAT_SAND, ENV_PARTICLE_DUST, 0.015f},
    {MAT_PODZOL, ENV_PARTICLE_DUST, 0.006f},
    {MAT_DIRT, ENV_PARTICLE_DUST, 0.005f},
    {MAT_GRAVEL, ENV_PARTICLE_DUST, 0.005f},
    {MAT_SANDSTONE, ENV_PARTICLE_DUST, 0.005f},
    /* Flowers: pollen, and bees working them */
    {MAT_FLOWER_RED, ENV_PARTICLE_POLLEN, 0.15f},
    {MAT_FLOWER_RED, ENV_PARTICLE_BEE, 0.03f},
    {MAT_FLOWER_BLUE, ENV_PARTICLE_POLLEN, 0.15f},
    {MAT_FLOWER_BLUE, ENV_PARTICLE_BEE, 0.03f},
    {MAT_FLOWER_YELLOW, ENV_PARTICLE_POLLEN, 0.15f},
    {MAT_FLOWER_YELLOW, ENV_PARTICLE_BEE, 0.03f},
    /* Mist rising off open water */
    {MAT_WATER, ENV_PARTICLE_STEAM, 0.002f},
    {MAT_SWAMP_WATER, ENV_PARTICLE_STEAM, 0.002f},
};
const int32_t g_env_emitter_rule_count = (int32_t)(sizeof(g_env_emitter_rules) / sizeof(g_env_emitter_rules[0]));

/* Each type's share of the emitters */
#define EMITTER_BUDGET_LEAF 1280
#define EMITTER_BUDGET_DUST 896
#define EMITTER_BUDGET_FIREFLY 896
#define EMITTER_BUDGET_POLLEN 512
#define EMITTER_BUDGET_BEE 256
#define EMITTER_BUDGET_STEAM 256
STATIC_ASSERT(EMITTER_BUDGET_LEAF + EMITTER_BUDGET_DUST + EMITTER_BUDGET_FIREFLY + EMITTER_BUDGET_POLLEN +
                      EMITTER_BUDGET_BEE + EMITTER_BUDGET_STEAM <= ENV_EMITTER_MAX,
              "Emitter budgets exceed the emitter array");
static const int32_t s_emitter_budget[ENV_PARTICLE_TYPE_COUNT] = {
    [ENV_PARTICLE_LEAF] = EMITTER_BUDGET_LEAF,       [ENV_PARTICLE_DUST] = EMITTER_BUDGET_DUST,
    [ENV_PARTICLE_FIREFLY] = EMITTER_BUDGET_FIREFLY, [ENV_PARTICLE_POLLEN] = EMITTER_BUDGET_POLLEN,
    [ENV_PARTICLE_BEE] = EMITTER_BUDGET_BEE,         [ENV_PARTICLE_STEAM] = EMITTER_BUDGET_STEAM,
};

#define EMITTER_SEED 54321u
/* Draws of the ground under a canopy, apart from those of the column's top voxel */
#define EMITTER_GROUND_SEED 98765u

/* An open voxel that emits: rank orders the candidates of a type for the sample */
typedef struct
{
    uint32_t rank;
    int32_t gx, gz;
    int16_t gy;
    uint8_t type;
    uint8_t material;
} EmitterCandidate;

/* Candidates of one column: its top voxel and the ground under a canopy */
#define EMITTER_SURFACES_PER_COLUMN 2

typedef struct
{
    const VoxelVolume *vol;
    uint32_t seed;
    /* Rules by material: rule_index[rule_start[m]..rule_start[m + 1]) in table order */
    int32_t rule_start[257];
    int32_t rule_index[64];
    /* Materials whose particles are leaves: the scan looks through them for the ground */
    bool canopy[256];
    EmitterCandidate **found; /* Per chunk column, NULL when none */
    int32_t *found_count;
} EmitterScan;

STATIC_ASSERT(sizeof(g_env_emitter_rules) / sizeof(g_env_emitter_rules[0]) <= 64, "Emitter rule index too small");

/* Two independent 32-bit draws of a column (splitmix64): one picks its rule, the other
 * ranks it among its type, so the rank is uniform whichever rule fired */
static uint64_t column_hash(uint32_t seed, int32_t gx, int32_t gz)
{
    uint64_t h = ((uint64_t)seed << 32) ^ ((uint64_t)(uint32_t)gx * 0x9E3779B97F4A7C15ull) ^
                 ((uint64_t)(uint32_t)gz * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return h;
}

/* Adds the candidate of open voxel (gx, gy, gz) of material mat if one of its rules fires */
static void add_candidate(const EmitterScan *scan, uint32_t seed, int32_t gx, int32_t gy, int32_t gz, uint8_t mat,
                          EmitterCandidate *out, int32_t *count)
{
    if (scan->rule_start[mat] == scan->rule_start[mat + 1])
        return;
    uint64_t h = column_hash(seed, gx, gz);
    float roll = (float)(uint32_t)(h & 0xFFFFFFu) * (1.0f / 16777216.0f);
    for (int32_t k = scan->rule_start[mat]; k < scan->rule_start[mat + 1]; k++)
    {
        const EnvEmitterRule *rule = &g_env_emitter_rules[scan->rule_index[k]];
        if (roll < rule->density)
        {
            EmitterCandidate *c = &out[(*count)++];
            c->rank = (uint32_t)(h >> 32);
            c->gx = gx;
            c->gz = gz;
            c->gy = (int16_t)gy;
            c->type = rule->type;
            c->material = mat;
            return;
        }
        roll -= rule->density;
    }
}

/* Finds the emitting voxels of whole chunk columns; reads only, and writes only its own
 * chunk column's list */
static void scan_emitter_chunk_columns(void *ctx, int32_t begin, int32_t end)
{
    const EmitterScan *scan = (const EmitterScan *)ctx;
    const VoxelVolume *vol = scan->vol;
    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    EmitterCandidate local[CHUNK_SIZE * CHUNK_SIZE * EMITTER_SURFACES_PER_COLUMN];

    for (int32_t cc = begin; cc < end; cc++)
    {
        const int32_t cx = cc % vol->chunks_x;
        const int32_t cz = cc / vol->chunks_x;
        int32_t count = 0;
        for (int32_t lz = 0; lz < CHUNK_SIZE; lz++)
        {
            for (int32_t lx = 0; lx < CHUNK_SIZE; lx++)
            {
                const int32_t gx = (cx << CHUNK_SIZE_BITS) + lx;
                const int32_t gz = (cz << CHUNK_SIZE_BITS) + lz;
                uint8_t mat = MAT_AIR;
                int32_t gy = volume_column_top(vol, gx, gz, &mat);
                if (gy < 0 || gy + 1 >= total_y)
                    continue;
                add_candidate(scan, scan->seed, gx, gy, gz, mat, local, &count);
                if (!scan->canopy[mat])
                    continue;

                /* Down through the canopy to the first voxel that is not part of one: the
                 * ground, which emits when open to the air (not under a trunk or a bough) */
                for (;;)
                {
                    uint8_t below = MAT_AIR;
                    int32_t by = volume_column_highest(vol, gx, gz, gy - 1, &below);
                    if (by < 0)
                        break;
                    if (!scan->canopy[below])
                    {
                        if (by < gy - 1)
                            add_candidate(scan, scan->seed + EMITTER_GROUND_SEED, gx, by, gz, below, local, &count);
                        break;
                    }
                    gy = by;
                }
            }
        }

        scan->found[cc] = NULL;
        scan->found_count[cc] = 0;
        if (count > 0)
        {
            scan->found[cc] = (EmitterCandidate *)malloc((size_t)count * sizeof(EmitterCandidate));
            if (scan->found[cc])
            {
                memcpy(scan->found[cc], local, (size_t)count * sizeof(EmitterCandidate));
                scan->found_count[cc] = count;
            }
        }
    }
}

/* Type, then rank, then position: a total order, so the sample is the same however the
 * candidates were found */
static int compare_candidates(const void *a, const void *b)
{
    const EmitterCandidate *x = (const EmitterCandidate *)a;
    const EmitterCandidate *y = (const EmitterCandidate *)b;
    if (x->type != y->type)
        return x->type < y->type ? -1 : 1;
    if (x->rank != y->rank)
        return x->rank < y->rank ? -1 : 1;
    if (x->gz != y->gz)
        return x->gz < y->gz ? -1 : 1;
    if (x->gx != y->gx)
        return x->gx < y->gx ? -1 : 1;
    return (x->gy > y->gy) - (x->gy < y->gy);
}

void env_particles_register_emitters(EnvParticleSystem *sys,
                                      const VoxelVolume *vol,
                                      float voxel_size,
                                      JobSystem *jobs)
{
    if (!sys || !vol)
        return;

    sys->emitter_count = 0;

    const int32_t column_count = vol->chunks_x * vol->chunks_z;
    EmitterScan scan;
    scan.vol = vol;
    scan.seed = sys->seed + EMITTER_SEED;
    memset(scan.rule_start, 0, sizeof(scan.rule_start));
    memset(scan.canopy, 0, sizeof(scan.canopy));
    for (int32_t r = 0; r < g_env_emitter_rule_count; r++)
    {
        scan.rule_start[g_env_emitter_rules[r].material + 1]++;
        if (g_env_emitter_rules[r].type == ENV_PARTICLE_LEAF)
            scan.canopy[g_env_emitter_rules[r].material] = true;
    }
    for (int32_t m = 0; m < 256; m++)
        scan.rule_start[m + 1] += scan.rule_start[m];
    int32_t cursor[256];
    memcpy(cursor, scan.rule_start, sizeof(cursor));
    for (int32_t r = 0; r < g_env_emitter_rule_count; r++)
        scan.rule_index[cursor[g_env_emitter_rules[r].material]++] = r;

    scan.found = (EmitterCandidate **)calloc((size_t)column_count, sizeof(EmitterCandidate *));
    scan.found_count = (int32_t *)calloc((size_t)column_count, sizeof(int32_t));
    if (!scan.found || !scan.found_count)
    {
        free((void *)scan.found);
        free(scan.found_count);
        return;
    }
    job_parallel_for(jobs, column_count, 0, scan_emitter_chunk_columns, &scan);

    int32_t total = 0;
    for (int32_t cc = 0; cc < column_count; cc++)
        total += scan.found_count[cc];
    EmitterCandidate *all = (EmitterCandidate *)malloc((size_t)(total > 0 ? total : 1) * sizeof(EmitterCandidate));
    if (all)
    {
        int32_t n = 0;
        for (int32_t cc = 0; cc < column_count; cc++)
        {
            if (scan.found_count[cc] > 0)
                memcpy(all + n, scan.found[cc], (size_t)scan.found_count[cc] * sizeof(EmitterCandidate));
            n += scan.found_count[cc];
        }

        /* The lowest ranks of each type, up to its budget: an even random sample of the
         * type's candidates, wherever in the world they are */
        qsort(all, (size_t)total, sizeof(EmitterCandidate), compare_candidates);
        int32_t taken[ENV_PARTICLE_TYPE_COUNT] = {0};
        for (int32_t i = 0; i < total && sys->emitter_count < ENV_EMITTER_MAX; i++)
        {
            const EmitterCandidate *c = &all[i];
            if (taken[c->type] >= s_emitter_budget[c->type])
                continue;
            taken[c->type]++;
            EnvEmitter *e = &sys->emitters[sys->emitter_count++];
            e->position = vec3_create(vol->bounds.min_x + ((float)c->gx + 0.5f) * voxel_size,
                                      vol->bounds.min_y + (float)(c->gy + 1) * voxel_size,
                                      vol->bounds.min_z + ((float)c->gz + 0.5f) * voxel_size);
            e->gx = c->gx;
            e->gy = c->gy;
            e->gz = c->gz;
            e->type = c->type;
            e->source_material = c->material;
        }
        free(all);
    }

    for (int32_t cc = 0; cc < column_count; cc++)
        free(scan.found[cc]);
    free((void *)scan.found);
    free(scan.found_count);
}

void env_particles_set_wind(EnvParticleSystem *sys, Vec3 direction, float strength)
{
    if (!sys)
        return;
    sys->wind_velocity = vec3_scale(direction, strength);
}
