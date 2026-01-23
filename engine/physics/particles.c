#include "particles.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

ParticleSystem* particle_system_create(Bounds3D bounds) {
    ParticleSystem* sys = (ParticleSystem*)calloc(1, sizeof(ParticleSystem));
    if (!sys) return NULL;

    sys->count = 0;
    sys->next_slot = 0;
    sys->bounds = bounds;
    sys->gravity = vec3_create(0.0f, -18.0f, 0.0f);
    sys->damping = 0.985f;
    sys->restitution = 0.45f;
    sys->floor_friction = 0.88f;
    sys->enable_particle_collision = true;

    /* Cell size = 4x typical particle radius to reduce multi-cell spans */
    float cell_size = 0.25f;
    spatial_hash_init(&sys->collision_grid, cell_size, bounds);

    return sys;
}

void particle_system_destroy(ParticleSystem* sys) {
    free(sys);
}

void particle_system_clear(ParticleSystem* sys) {
    sys->count = 0;
    sys->next_slot = 0;
    sys->active_count = 0;
}

Particle* particle_system_add_slot(ParticleSystem* sys) {
    uint32_t slot = sys->next_slot;
    sys->next_slot = (sys->next_slot + 1) % PARTICLE_MAX_COUNT;
    if (sys->count < PARTICLE_MAX_COUNT) {
        sys->count++;
    }
    return &sys->particles[slot];
}

int32_t particle_system_add(ParticleSystem* sys, RngState *rng, Vec3 position, Vec3 velocity, Vec3 color, float radius) {
    Particle* p = particle_system_add_slot(sys);
    /* active_count tracking:
     * - If slot was inactive, we're adding a new particle: increment
     * - If slot was active (at capacity, overwriting oldest): no change */
    if (!p->active) sys->active_count++;
    p->position = position;
    p->prev_position = position;
    p->velocity = velocity;
    p->rotation = vec3_zero();
    p->prev_rotation = vec3_zero();
    p->angular_velocity = vec3_create(
        rng_signed_half(rng) * 20.0f,
        rng_signed_half(rng) * 20.0f,
        rng_signed_half(rng) * 20.0f
    );
    p->color = color;
    p->radius = radius;
    p->lifetime = 0.0f;
    p->active = true;
    p->settled = false;
    return (int32_t)(p - sys->particles);
}

static void resolve_particle_boundary(Particle* p, const Bounds3D* bounds, float restitution) {
    /* Floor collision only - no invisible walls */
    if (p->position.y - p->radius < bounds->min_y) {
        p->position.y = bounds->min_y + p->radius;
        p->velocity.y = -p->velocity.y * restitution;
    }
}

static void resolve_particle_collision(Particle* a, Particle* b, float restitution) {
    Vec3 delta = vec3_sub(b->position, a->position);
    float dist = vec3_length(delta);
    float min_dist = a->radius + b->radius;

    if (dist >= min_dist || dist < 0.0001f) return;

    Vec3 normal = vec3_scale(delta, 1.0f / dist);
    float overlap = min_dist - dist;

    a->position = vec3_sub(a->position, vec3_scale(normal, overlap * 0.5f));
    b->position = vec3_add(b->position, vec3_scale(normal, overlap * 0.5f));

    Vec3 rel_vel = vec3_sub(a->velocity, b->velocity);
    float vel_along_normal = vec3_dot(rel_vel, normal);

    if (vel_along_normal > 0.0f) return;

    float j = -(1.0f + restitution) * vel_along_normal * 0.5f;
    Vec3 impulse = vec3_scale(normal, j);

    a->velocity = vec3_add(a->velocity, impulse);
    b->velocity = vec3_sub(b->velocity, impulse);
}

void particle_system_update(ParticleSystem* sys, float dt) {
    /* No compaction needed - circular buffer overwrites oldest particles when at capacity */

    /* Safe max velocity to prevent tunneling (based on typical particle radius) */
    float max_velocity = 0.03f / dt;
    if (max_velocity < 10.0f) max_velocity = 10.0f;
    if (max_velocity > 30.0f) max_velocity = 30.0f;

    /* Save previous positions for interpolation before updating physics */
    for (int32_t i = 0; i < sys->count; i++) {
        Particle* p = &sys->particles[i];
        if (!p->active) continue;
        p->prev_position = p->position;
        p->prev_rotation = p->rotation;
    }

    /* Update lifetime for age tracking (used for young particle priority).
     * No auto-expiration - particles are removed via circular buffer when at capacity. */
    for (int32_t i = 0; i < sys->count; i++) {
        Particle* p = &sys->particles[i];
        if (!p->active) continue;
        p->lifetime += dt;
    }

    /* Age-prioritized budgeted physics update:
     * Pass 1: Always update young particles (fast-moving, need frequent updates)
     * Pass 2: Use remaining budget for older particles (round-robin) */
    int32_t budget = PARTICLE_MAX_UPDATES_PER_TICK;
    int32_t processed = 0;

    /* Pass 1: Young particles always get priority (unbounded - they're time-limited) */
    for (int32_t i = 0; i < sys->count; i++) {
        Particle* p = &sys->particles[i];
        if (!p->active || p->settled) continue;
        if (p->lifetime > PARTICLE_YOUNG_AGE_THRESHOLD) continue;

        p->velocity = vec3_add(p->velocity, vec3_scale(sys->gravity, dt));

        float speed_sq = vec3_length_sq(p->velocity);
        if (speed_sq > max_velocity * max_velocity) {
            float speed = sqrtf(speed_sq);
            p->velocity = vec3_scale(p->velocity, max_velocity / speed);
        }

        p->velocity = vec3_scale(p->velocity, sys->damping);

        float floor_dist = p->position.y - p->radius - sys->bounds.min_y;
        if (floor_dist < 0.05f) {
            p->velocity.x *= sys->floor_friction;
            p->velocity.z *= sys->floor_friction;
            p->angular_velocity = vec3_scale(p->angular_velocity, 0.9f);
        }

        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
        p->rotation = vec3_add(p->rotation, vec3_scale(p->angular_velocity, dt));
        p->angular_velocity = vec3_scale(p->angular_velocity, 0.995f);

        resolve_particle_boundary(p, &sys->bounds, sys->restitution);
        /* No processed++ here - young particles are unbounded (time-limited to ~1s) */
    }

    /* Pass 2: Older particles with remaining budget (round-robin) */
    int32_t cursor = sys->update_cursor;
    int32_t checked = 0;
    while (processed < budget && checked < sys->count) {
        if (cursor >= sys->count) cursor = 0;

        Particle* p = &sys->particles[cursor];
        cursor++;
        checked++;

        /* Skip inactive, settled, or young (already processed) */
        if (!p->active || p->settled || p->lifetime <= PARTICLE_YOUNG_AGE_THRESHOLD) continue;

        p->velocity = vec3_add(p->velocity, vec3_scale(sys->gravity, dt));

        float speed_sq = vec3_length_sq(p->velocity);
        if (speed_sq > max_velocity * max_velocity) {
            float speed = sqrtf(speed_sq);
            p->velocity = vec3_scale(p->velocity, max_velocity / speed);
        }

        p->velocity = vec3_scale(p->velocity, sys->damping);

        float floor_dist = p->position.y - p->radius - sys->bounds.min_y;
        if (floor_dist < 0.05f) {
            p->velocity.x *= sys->floor_friction;
            p->velocity.z *= sys->floor_friction;
            p->angular_velocity = vec3_scale(p->angular_velocity, 0.9f);
        }

        p->position = vec3_add(p->position, vec3_scale(p->velocity, dt));
        p->rotation = vec3_add(p->rotation, vec3_scale(p->angular_velocity, dt));
        p->angular_velocity = vec3_scale(p->angular_velocity, 0.995f);

        resolve_particle_boundary(p, &sys->bounds, sys->restitution);
        processed++;
    }
    sys->update_cursor = cursor;

    /* Spatial hash collision: O(n) average, bounded by pair budget */
    if (sys->enable_particle_collision) {
        spatial_hash_clear(&sys->collision_grid);

        /* Insert active non-settled particles into grid */
        for (int32_t i = 0; i < sys->count; i++) {
            if (!sys->particles[i].active || sys->particles[i].settled) continue;
            spatial_hash_insert(&sys->collision_grid, i,
                               sys->particles[i].position, sys->particles[i].radius);
        }

        /* Check collisions using spatial hash (capped to prevent frame spikes) */
        int32_t pair_budget = PARTICLE_MAX_COLLISION_PAIRS;
        for (int32_t i = 0; i < sys->count && pair_budget > 0; i++) {
            if (!sys->particles[i].active || sys->particles[i].settled) continue;

            int32_t nearby[SPATIAL_HASH_MAX_PER_CELL];
            int32_t nearby_count = spatial_hash_query(&sys->collision_grid,
                sys->particles[i].position, sys->particles[i].radius * 2.0f,
                nearby, SPATIAL_HASH_MAX_PER_CELL);

            for (int32_t n = 0; n < nearby_count && pair_budget > 0; n++) {
                int32_t j = nearby[n];
                if (j <= i) continue;
                if (!sys->particles[j].active || sys->particles[j].settled) continue;

                resolve_particle_collision(&sys->particles[i], &sys->particles[j], sys->restitution);
                pair_budget--;
            }
        }
    }

    for (int32_t i = 0; i < sys->count; i++) {
        Particle* p = &sys->particles[i];
        if (!p->active || p->settled) continue;

        float speed = vec3_length(p->velocity);
        float floor_dist = p->position.y - p->radius - sys->bounds.min_y;
        if (speed < PARTICLE_SETTLE_VELOCITY && floor_dist < 0.02f) {
            p->settled = true;
            p->velocity = vec3_zero();
        }
    }
}

int32_t particle_system_spawn_explosion(ParticleSystem* sys, RngState *rng, Vec3 center, float radius,
                                         Vec3 color, int32_t count, float force) {
    int32_t spawned = 0;

    for (int32_t i = 0; i < count; i++) {
        float theta = rng_float(rng) * 2.0f * K_PI;
        float phi = rng_float(rng) * K_PI;
        float r = rng_float(rng) * radius * 0.8f;

        float sin_phi = sinf(phi);
        Vec3 offset = vec3_create(
            r * sin_phi * cosf(theta),
            r * cosf(phi),
            r * sin_phi * sinf(theta)
        );

        Vec3 dir = vec3_length(offset) > 0.001f ? vec3_normalize(offset) : vec3_create(0.0f, 1.0f, 0.0f);

        float speed_variation = 0.5f + rng_float(rng) * 1.0f;
        Vec3 vel = vec3_scale(dir, force * speed_variation);

        vel.y += force * 0.3f * rng_float(rng);

        float color_variation = 0.9f + rng_float(rng) * 0.2f;
        Vec3 particle_color = vec3_scale(color, color_variation);
        particle_color.x = clampf(particle_color.x, 0.0f, 1.0f);
        particle_color.y = clampf(particle_color.y, 0.0f, 1.0f);
        particle_color.z = clampf(particle_color.z, 0.0f, 1.0f);

        /* Use circular buffer - overwrites oldest when at capacity */
        Particle* p = particle_system_add_slot(sys);
        if (!p->active) sys->active_count++;
        p->position = vec3_add(center, offset);
        p->prev_position = p->position;
        p->velocity = vel;
        p->rotation = vec3_zero();
        p->prev_rotation = vec3_zero();
        p->angular_velocity = vec3_create(
            rng_signed_half(rng) * 20.0f,
            rng_signed_half(rng) * 20.0f,
            rng_signed_half(rng) * 20.0f
        );
        p->color = particle_color;
        p->radius = 0.04f + rng_float(rng) * 0.03f;
        p->lifetime = 0.0f;
        p->active = true;
        p->settled = false;

        spawned++;
    }

    return spawned;
}

int32_t particle_system_spawn_at_impact(ParticleSystem* sys, RngState *rng, Vec3 impact_point, Vec3 ball_center,
                                         float ball_radius, Vec3 color, int32_t count, float force) {
    int32_t spawned = 0;

    Vec3 impact_dir = vec3_sub(impact_point, ball_center);
    float impact_len = vec3_length(impact_dir);
    if (impact_len > 0.001f) {
        impact_dir = vec3_scale(impact_dir, 1.0f / impact_len);
    } else {
        impact_dir = vec3_create(0.0f, 1.0f, 0.0f);
    }

    for (int32_t i = 0; i < count; i++) {
        float spread_theta = rng_signed_half(rng) * K_PI * 0.8f;
        float spread_phi = rng_float(rng) * 2.0f * K_PI;
        float r = rng_float(rng) * ball_radius * 0.3f;

        Vec3 up = fabsf(impact_dir.y) < 0.9f ? vec3_create(0.0f, 1.0f, 0.0f) : vec3_create(1.0f, 0.0f, 0.0f);
        Vec3 right = vec3_normalize(vec3_cross(up, impact_dir));
        Vec3 tangent = vec3_cross(impact_dir, right);

        Vec3 dir;
        dir.x = impact_dir.x * cosf(spread_theta) + right.x * sinf(spread_theta) * cosf(spread_phi) + tangent.x * sinf(spread_theta) * sinf(spread_phi);
        dir.y = impact_dir.y * cosf(spread_theta) + right.y * sinf(spread_theta) * cosf(spread_phi) + tangent.y * sinf(spread_theta) * sinf(spread_phi);
        dir.z = impact_dir.z * cosf(spread_theta) + right.z * sinf(spread_theta) * cosf(spread_phi) + tangent.z * sinf(spread_theta) * sinf(spread_phi);
        dir = vec3_normalize(dir);

        Vec3 offset = vec3_scale(dir, r);
        offset = vec3_add(offset, vec3_scale(impact_dir, ball_radius * 0.1f));

        float speed_variation = 0.5f + rng_float(rng) * 1.0f;
        Vec3 vel = vec3_scale(dir, force * speed_variation);

        float color_variation = 0.85f + rng_float(rng) * 0.3f;
        Vec3 particle_color = vec3_scale(color, color_variation);
        particle_color.x = clampf(particle_color.x, 0.0f, 1.0f);
        particle_color.y = clampf(particle_color.y, 0.0f, 1.0f);
        particle_color.z = clampf(particle_color.z, 0.0f, 1.0f);

        /* Use circular buffer - overwrites oldest when at capacity */
        Particle* p = particle_system_add_slot(sys);
        if (!p->active) sys->active_count++;
        p->position = vec3_add(impact_point, offset);
        p->prev_position = p->position;
        p->velocity = vel;
        p->rotation = vec3_zero();
        p->prev_rotation = vec3_zero();
        p->angular_velocity = vec3_create(
            rng_signed_half(rng) * 20.0f,
            rng_signed_half(rng) * 20.0f,
            rng_signed_half(rng) * 20.0f
        );
        p->color = particle_color;
        p->radius = 0.03f + rng_float(rng) * 0.04f;
        p->lifetime = 0.0f;
        p->active = true;
        p->settled = false;
        spawned++;
    }

    return spawned;
}

int32_t particle_system_get_settled(ParticleSystem* sys, Particle* out_settled, int32_t max_count) {
    int32_t found = 0;
    for (int32_t i = 0; i < sys->count && found < max_count; i++) {
        if (sys->particles[i].active && sys->particles[i].settled) {
            out_settled[found++] = sys->particles[i];
        }
    }
    return found;
}

void particle_system_remove_settled(ParticleSystem* sys) {
    int32_t write = 0;
    for (int32_t read = 0; read < sys->count; read++) {
        if (sys->particles[read].active && !sys->particles[read].settled) {
            if (write != read) {
                sys->particles[write] = sys->particles[read];
            }
            write++;
        }
    }
    sys->count = write;
    sys->active_count = write;  /* All remaining are active non-settled */
}

bool particle_system_pickup_nearest(ParticleSystem* sys, Vec3 position, float max_dist, Vec3* out_color) {
    int32_t nearest_idx = -1;
    float nearest_dist = max_dist;
    
    for (int32_t i = 0; i < sys->count; i++) {
        if (!sys->particles[i].active || !sys->particles[i].settled) continue;
        
        Vec3 to_particle = vec3_sub(sys->particles[i].position, position);
        to_particle.y = 0.0f;
        float dist = vec3_length(to_particle);
        
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_idx = i;
        }
    }

    if (nearest_idx < 0) {
        nearest_dist = max_dist;
        for (int32_t i = 0; i < sys->count; i++) {
            if (!sys->particles[i].active) continue;
            
            Vec3 to_particle = vec3_sub(sys->particles[i].position, position);
            to_particle.y = 0.0f;
            float dist = vec3_length(to_particle);
            
            if (dist < nearest_dist) {
                nearest_dist = dist;
                nearest_idx = i;
            }
        }
    }
    
    if (nearest_idx < 0) return false;

    *out_color = sys->particles[nearest_idx].color;
    sys->particles[nearest_idx].active = false;
    sys->active_count--;

    return true;
}
