#include "particles.h"
#include <assert.h>
#include <stdlib.h>

STATIC_ASSERT(PARTICLE_MAX_COUNT <= SPATIAL_HASH_MAX_OBJECTS, "particle indices must fit the collision grid");

ParticleSystem *particle_system_create(Bounds3D bounds)
{
    ParticleSystem *sys = (ParticleSystem *)calloc(1, sizeof(ParticleSystem));
    if (!sys)
        return NULL;

    sys->gravity = vec3_create(0.0f, -18.0f, 0.0f);
    sys->damping = 0.985f;
    sys->restitution = 0.45f;
    sys->floor_friction = 0.88f;

    /* Cell size = 4x typical particle radius to reduce multi-cell spans */
    spatial_hash_init(&sys->collision_grid, 0.25f, bounds);

    return sys;
}

void particle_system_destroy(ParticleSystem *sys)
{
    free(sys);
}

static void particle_swap(ParticleSystem *sys, int32_t a, int32_t b)
{
    if (a == b)
        return;
    Particle t = sys->particles[a];
    sys->particles[a] = sys->particles[b];
    sys->particles[b] = t;
}

void particle_system_add(ParticleSystem *sys, Vec3 position, Vec3 velocity, Vec3 color, float radius)
{
    /* The new particle joins the moving range at awake_count; the settled particle
     * there moves to the end of the settled range, or at capacity replaces the one at
     * the end (settled ranges fill from the front, so that one settled early) */
    int32_t slot;
    if (sys->count < PARTICLE_MAX_COUNT)
    {
        sys->particles[sys->count] = sys->particles[sys->awake_count];
        slot = sys->awake_count++;
        sys->count++;
    }
    else if (sys->awake_count < sys->count)
    {
        sys->particles[sys->count - 1] = sys->particles[sys->awake_count];
        slot = sys->awake_count++;
    }
    else
    {
        slot = sys->replace_cursor;
        sys->replace_cursor = (slot + 1) % PARTICLE_MAX_COUNT;
    }

    Particle *p = &sys->particles[slot];
    p->position = position;
    p->prev_position = position;
    p->velocity = velocity;
    p->color = vec3_srgb_to_linear(color);
    p->radius = radius;
    p->lifetime = 0.0f;
    p->moved = 0.0f;
    p->expire_time = 0.0f;
    p->active = true;
    p->settled = false;
}

static bool particle_near_surface(const Particle *p, const VoxelVolume *terrain, const VoxelObjectWorld *objects)
{
    Vec3 below = vec3_add(p->position, vec3_create(0.0f, -p->radius - 0.05f, 0.0f));
    if (terrain && volume_is_solid_at(terrain, below))
        return true;
    return objects && voxel_object_world_test_point(objects, below).hit;
}

static Vec3 estimate_terrain_normal(const VoxelVolume *vol, Vec3 pos, float vs)
{
    Vec3 n = vec3_zero();
    if (!volume_is_solid_at(vol, vec3_add(pos, vec3_create(vs, 0.0f, 0.0f))))
        n.x += 1.0f;
    if (!volume_is_solid_at(vol, vec3_add(pos, vec3_create(-vs, 0.0f, 0.0f))))
        n.x -= 1.0f;
    if (!volume_is_solid_at(vol, vec3_add(pos, vec3_create(0.0f, vs, 0.0f))))
        n.y += 1.0f;
    if (!volume_is_solid_at(vol, vec3_add(pos, vec3_create(0.0f, -vs, 0.0f))))
        n.y -= 1.0f;
    if (!volume_is_solid_at(vol, vec3_add(pos, vec3_create(0.0f, 0.0f, vs))))
        n.z += 1.0f;
    if (!volume_is_solid_at(vol, vec3_add(pos, vec3_create(0.0f, 0.0f, -vs))))
        n.z -= 1.0f;
    float len = vec3_length(n);
    return len > 0.001f ? vec3_scale(n, 1.0f / len) : vec3_create(0.0f, 1.0f, 0.0f);
}

static void resolve_particle_terrain(Particle *p, const VoxelVolume *vol, const VoxelObjectWorld *objects,
                                     float restitution, float friction)
{
    bool hit_terrain = vol && volume_is_solid_at(vol, p->position);
    Vec3 normal = vec3_create(0.0f, 1.0f, 0.0f);

    if (!hit_terrain)
    {
        VoxelObjectPointTest test = objects ? voxel_object_world_test_point(objects, p->position)
                                            : (VoxelObjectPointTest){0};
        if (!test.hit)
            return;
        normal = test.surface_normal;
    }

    /* Started inside terrain too: push upward to escape, or retire the particle */
    if (hit_terrain && volume_is_solid_at(vol, p->prev_position))
    {
        Vec3 escape = p->prev_position;
        for (int32_t push = 0; push < 5; push++)
        {
            escape.y += vol->voxel_size;
            if (!volume_is_solid_at(vol, escape))
            {
                p->position = escape;
                p->velocity.y = fabsf(p->velocity.y) * 0.3f;
                return;
            }
        }
        p->active = false;
        return;
    }

    p->position = p->prev_position;
    if (hit_terrain)
        normal = estimate_terrain_normal(vol, p->position, vol->voxel_size);

    float vn = vec3_dot(p->velocity, normal);
    if (vn < 0.0f)
        p->velocity = vec3_sub(p->velocity, vec3_scale(normal, (1.0f + restitution) * vn));

    /* Kill micro-bounces: a tiny rebound is dropped */
    float vn_after = vec3_dot(p->velocity, normal);
    if (vn_after > 0.0f && vn_after < PARTICLE_SETTLE_VELOCITY)
    {
        p->velocity = vec3_sub(p->velocity, vec3_scale(normal, vn_after));
        vn_after = 0.0f;
    }

    /* Surface friction on the tangential component */
    Vec3 normal_component = vec3_scale(normal, vn_after);
    Vec3 tangent_vel = vec3_sub(p->velocity, normal_component);
    p->velocity = vec3_add(normal_component, vec3_scale(tangent_vel, friction));
}

static void resolve_particle_collision(Particle *a, Particle *b, float restitution)
{
    Vec3 delta = vec3_sub(b->position, a->position);
    float dist = vec3_length(delta);
    float min_dist = a->radius + b->radius;
    if (dist >= min_dist || dist < 0.0001f)
        return;

    Vec3 normal = vec3_scale(delta, 1.0f / dist);
    float overlap = min_dist - dist;
    a->position = vec3_sub(a->position, vec3_scale(normal, overlap * 0.5f));
    b->position = vec3_add(b->position, vec3_scale(normal, overlap * 0.5f));

    float vel_along_normal = vec3_dot(vec3_sub(a->velocity, b->velocity), normal);
    if (vel_along_normal > 0.0f)
        return;

    Vec3 impulse = vec3_scale(normal, -(1.0f + restitution) * vel_along_normal * 0.5f);
    a->velocity = vec3_add(a->velocity, impulse);
    b->velocity = vec3_sub(b->velocity, impulse);
}

static void settle_particle(Particle *p)
{
    p->settled = true;
    p->velocity = vec3_zero();
}

static void step_particle(const ParticleSystem *sys, Particle *p, float dt, float max_velocity,
                          const VoxelVolume *terrain, const VoxelObjectWorld *objects)
{
    /* Fell out of the world: nothing below can stop it */
    if (p->position.y < sys->collision_grid.bounds.min_y)
    {
        p->active = false;
        return;
    }

    /* Settle on the velocity left from the last step, before gravity re-energizes it. Past
     * the young age, also on the distance actually moved: debris pinned in a slot or wedged
     * against a step has every step undone by the terrain while its velocity stays up. */
    bool near_surface = particle_near_surface(p, terrain, objects);
    bool at_rest = vec3_length(p->velocity) < PARTICLE_SETTLE_VELOCITY ||
                   (p->lifetime > PARTICLE_YOUNG_AGE_THRESHOLD && p->moved < PARTICLE_SETTLE_VELOCITY * dt);
    if (near_surface && at_rest)
    {
        settle_particle(p);
        return;
    }

    p->velocity = vec3_add(p->velocity, vec3_scale(sys->gravity, dt));
    p->velocity = vec3_scale(vec3_clamp_length(p->velocity, max_velocity), sys->damping);
    if (near_surface)
    {
        p->velocity.x *= sys->floor_friction;
        p->velocity.z *= sys->floor_friction;
    }

    p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
    resolve_particle_terrain(p, terrain, objects, sys->restitution, sys->floor_friction);

    if (vec3_length(p->velocity) < PARTICLE_SETTLE_VELOCITY && particle_near_surface(p, terrain, objects))
        settle_particle(p);
}

/* Moves settled and retired particles out of the moving range. Stepping only flags
 * them; reordering after the steps keeps the step loops simple. */
static void compact_moving(ParticleSystem *sys)
{
    assert(sys->awake_count >= 0 && sys->awake_count <= sys->count && sys->count <= PARTICLE_MAX_COUNT);
    for (int32_t i = sys->awake_count - 1; i >= 0; i--)
    {
        Particle *p = &sys->particles[i];
        if (p->active && !p->settled)
            continue;

        /* To the front of the settled range */
        particle_swap(sys, i, sys->awake_count - 1);
        sys->awake_count--;
        p = &sys->particles[sys->awake_count];
        if (p->active)
        {
            /* At rest from here: no more interpolation, and a lifetime from now */
            p->prev_position = p->position;
            p->expire_time = (float)(sys->time + PARTICLE_SETTLED_LIFETIME);
        }
        else
        {
            particle_swap(sys, sys->awake_count, sys->count - 1);
            sys->count--;
        }
    }
}

/* A budget of settled particles, round-robin: retire expired ones, wake those whose
 * support disappeared */
static void check_settled(ParticleSystem *sys, const VoxelVolume *terrain, const VoxelObjectWorld *objects)
{
    assert(sys->awake_count >= 0 && sys->awake_count <= sys->count && sys->count <= PARTICLE_MAX_COUNT);
    int32_t checks = sys->count - sys->awake_count;
    if (checks > PARTICLE_MAX_SETTLED_CHECKS_PER_TICK)
        checks = PARTICLE_MAX_SETTLED_CHECKS_PER_TICK;

    int32_t i = sys->settle_cursor;
    for (; checks > 0; checks--)
    {
        if (i < sys->awake_count || i >= sys->count)
            i = sys->awake_count;
        if (i >= sys->count)
            break;

        Particle *p = &sys->particles[i];
        if (sys->time >= (double)p->expire_time)
        {
            /* The last particle takes its place and is checked next */
            particle_swap(sys, i, sys->count - 1);
            sys->count--;
        }
        else if (!particle_near_surface(p, terrain, objects))
        {
            /* Back to the moving range; the first settled particle takes its place and
             * is checked next */
            p->settled = false;
            p->velocity = vec3_scale(sys->gravity, 0.01f);
            particle_swap(sys, i, sys->awake_count);
            sys->awake_count++;
        }
        else
        {
            i++;
        }
    }
    sys->settle_cursor = i;
}

void particle_system_update(ParticleSystem *sys, float dt,
                            const VoxelVolume *terrain,
                            const VoxelObjectWorld *objects)
{
    sys->time += dt;

    /* Speed cap against tunnelling through thin surfaces, from the typical radius */
    float max_velocity = clampf(0.03f / dt, 10.0f, 30.0f);

    /* Young particles (fast, just spawned) update every tick */
    for (int32_t i = 0; i < sys->awake_count; i++)
    {
        Particle *p = &sys->particles[i];
        p->moved = vec3_length(vec3_sub(p->position, p->prev_position));
        p->prev_position = p->position;
        p->lifetime += dt;
        if (p->lifetime <= PARTICLE_YOUNG_AGE_THRESHOLD)
            step_particle(sys, p, dt, max_velocity, terrain, objects);
    }

    /* Older ones share a per-tick budget, round-robin */
    int32_t cursor = sys->update_cursor;
    int32_t processed = 0;
    for (int32_t checked = 0; checked < sys->awake_count && processed < PARTICLE_MAX_UPDATES_PER_TICK; checked++)
    {
        if (cursor >= sys->awake_count)
            cursor = 0;
        Particle *p = &sys->particles[cursor++];
        if (!p->active || p->settled || p->lifetime <= PARTICLE_YOUNG_AGE_THRESHOLD)
            continue;

        step_particle(sys, p, dt, max_velocity, terrain, objects);
        processed++;
    }
    sys->update_cursor = cursor;

    compact_moving(sys);

    /* Particle-particle collision through the spatial hash, capped against frame spikes */
    spatial_hash_clear(&sys->collision_grid);
    for (int32_t i = 0; i < sys->awake_count; i++)
    {
        const Particle *p = &sys->particles[i];
        spatial_hash_insert(&sys->collision_grid, i, p->position, p->radius);
    }

    int32_t pair_budget = PARTICLE_MAX_COLLISION_PAIRS;
    for (int32_t i = 0; i < sys->awake_count && pair_budget > 0; i++)
    {
        Particle *a = &sys->particles[i];
        int32_t nearby[SPATIAL_HASH_MAX_PER_CELL];
        int32_t nearby_count = spatial_hash_query(&sys->collision_grid, a->position, a->radius * 2.0f,
                                                  nearby, SPATIAL_HASH_MAX_PER_CELL);
        for (int32_t n = 0; n < nearby_count && pair_budget > 0; n++)
        {
            int32_t j = nearby[n];
            if (j <= i)
                continue;
            resolve_particle_collision(a, &sys->particles[j], sys->restitution);
            pair_budget--;
        }
    }

    check_settled(sys, terrain, objects);
}
