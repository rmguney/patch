#include "physics.h"
#include <stdlib.h>
#include <string.h>

static void resolve_collision(Ball* a, Ball* b, float restitution, float friction) {
    Vec3 delta = vec3_sub(b->position, a->position);
    float dist = vec3_length(delta);
    float min_dist = a->radius + b->radius;

    if (dist >= min_dist || dist < K_EPSILON) {
        return;
    }

    Vec3 normal = vec3_scale(delta, 1.0f / dist);
    float overlap = min_dist - dist;
    float total_mass = a->mass + b->mass;
    float correction_factor = 0.8f;

    Vec3 separation = vec3_scale(normal, overlap * correction_factor);
    a->position = vec3_sub(a->position, vec3_scale(separation, b->mass / total_mass));
    b->position = vec3_add(b->position, vec3_scale(separation, a->mass / total_mass));

    Vec3 rel_vel = vec3_sub(a->velocity, b->velocity);
    float vel_along_normal = vec3_dot(rel_vel, normal);

    if (vel_along_normal > 0.0f) {
        return;
    }

    float inv_mass_sum = (1.0f / a->mass) + (1.0f / b->mass);
    float j = -(1.0f + restitution) * vel_along_normal / inv_mass_sum;

    Vec3 impulse = vec3_scale(normal, j);
    a->velocity = vec3_add(a->velocity, vec3_scale(impulse, 1.0f / a->mass));
    b->velocity = vec3_sub(b->velocity, vec3_scale(impulse, 1.0f / b->mass));

    Vec3 tangent = vec3_sub(rel_vel, vec3_scale(normal, vel_along_normal));
    float tangent_len = vec3_length(tangent);

    if (tangent_len > 0.001f) {
        tangent = vec3_scale(tangent, 1.0f / tangent_len);
        float jt = -vec3_dot(rel_vel, tangent) / inv_mass_sum;
        float max_friction = fabsf(j) * friction;
        jt = clampf(jt, -max_friction, max_friction);

        Vec3 friction_impulse = vec3_scale(tangent, jt);
        a->velocity = vec3_add(a->velocity, vec3_scale(friction_impulse, 1.0f / a->mass));
        b->velocity = vec3_sub(b->velocity, vec3_scale(friction_impulse, 1.0f / b->mass));
    }
}

static void apply_boundary(Ball* ball, const Bounds3D* bounds, float restitution) {
    if (ball->position.x - ball->radius < bounds->min_x) {
        ball->position.x = bounds->min_x + ball->radius;
        ball->velocity.x = -ball->velocity.x * restitution;
    }
    if (ball->position.x + ball->radius > bounds->max_x) {
        ball->position.x = bounds->max_x - ball->radius;
        ball->velocity.x = -ball->velocity.x * restitution;
    }

    if (ball->position.y - ball->radius < bounds->min_y) {
        ball->position.y = bounds->min_y + ball->radius;
        ball->velocity.y = -ball->velocity.y * restitution;
    }
    if (ball->position.y + ball->radius > bounds->max_y) {
        ball->position.y = bounds->max_y - ball->radius;
        ball->velocity.y = -ball->velocity.y * restitution;
    }

    if (ball->position.z - ball->radius < bounds->min_z) {
        ball->position.z = bounds->min_z + ball->radius;
        ball->velocity.z = -ball->velocity.z * restitution;
    }
    if (ball->position.z + ball->radius > bounds->max_z) {
        ball->position.z = bounds->max_z - ball->radius;
        ball->velocity.z = -ball->velocity.z * restitution;
    }
}

PhysicsWorld* physics_world_create(void) {
    PhysicsWorld* world = (PhysicsWorld*)calloc(1, sizeof(PhysicsWorld));
    if (!world) {
        return NULL;
    }

    world->ball_count = 0;
    world->gravity = vec3_create(0.0f, -25.0f, 0.0f);

    world->bounds.min_x = -10.0f;
    world->bounds.max_x = 10.0f;
    world->bounds.min_y = -7.5f;
    world->bounds.max_y = 7.5f;
    world->bounds.min_z = -5.0f;
    world->bounds.max_z = 5.0f;

    world->mouse_position = vec3_zero();
    world->mouse_radius = 4.5f;
    world->mouse_strength = 260.0f;
    world->mouse_active = false;

    world->damping = 0.98f;
    world->restitution = 0.25f;
    world->friction = 0.4f;
    world->floor_friction = 0.92f;

    world->min_velocity = 0.05f;
    world->max_velocity = 40.0f;

    return world;
}

void physics_world_destroy(PhysicsWorld* world) {
    free(world);
}

void physics_world_set_bounds(PhysicsWorld* world, Bounds3D bounds) {
    world->bounds = bounds;
}

void physics_world_set_mouse(PhysicsWorld* world, Vec3 position, bool active) {
    world->mouse_position = position;
    world->mouse_active = active;
}

int32_t physics_world_add_ball(PhysicsWorld* world, Vec3 position, float radius, Vec3 color) {
    if (world->ball_count >= PHYSICS_MAX_BALLS) {
        return -1;
    }

    int32_t index = world->ball_count++;
    Ball* ball = &world->balls[index];

    ball->position = position;
    ball->velocity = vec3_zero();
    ball->color = color;
    ball->radius = radius;
    ball->mass = radius * radius * radius;

    return index;
}

void physics_world_update(PhysicsWorld* world, float dt) {
    for (int32_t i = 0; i < world->ball_count; i++) {
        Ball* ball = &world->balls[i];

        ball->velocity = vec3_add(ball->velocity, vec3_scale(world->gravity, dt));

        if (world->mouse_active) {
            Vec3 to_mouse = vec3_sub(world->mouse_position, ball->position);
            float dist = vec3_length(to_mouse);

            if (dist < world->mouse_radius && dist > 0.001f) {
                float falloff = 1.0f - dist / world->mouse_radius;
                float force = world->mouse_strength * falloff * falloff;
                Vec3 force_dir = vec3_scale(to_mouse, -force / dist);
                ball->velocity = vec3_add(ball->velocity, vec3_scale(force_dir, dt));
            }
        }
    }

    for (int32_t iter = 0; iter < PHYSICS_SOLVER_ITERATIONS; iter++) {
        for (int32_t i = 0; i < world->ball_count; i++) {
            for (int32_t j = i + 1; j < world->ball_count; j++) {
                resolve_collision(&world->balls[i], &world->balls[j], 
                                  world->restitution, world->friction);
            }
        }
    }

    for (int32_t i = 0; i < world->ball_count; i++) {
        Ball* ball = &world->balls[i];

        ball->velocity = vec3_scale(ball->velocity, world->damping);

        float floor_dist = ball->position.y - ball->radius - world->bounds.min_y;
        if (floor_dist < 0.1f) {
            ball->velocity.x *= world->floor_friction;
            ball->velocity.z *= world->floor_friction;
        }

        float speed = vec3_length(ball->velocity);
        if (speed < world->min_velocity) {
            ball->velocity = vec3_zero();
        } else if (speed > world->max_velocity) {
            ball->velocity = vec3_scale(ball->velocity, world->max_velocity / speed);
        }

        ball->position = vec3_add(ball->position, vec3_scale(ball->velocity, dt));

        apply_boundary(ball, &world->bounds, world->restitution);
    }
}
