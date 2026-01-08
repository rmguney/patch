#ifndef PATCH_CORE_PHYSICS_H
#define PATCH_CORE_PHYSICS_H

#include "types.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHYSICS_MAX_BALLS 500
#define PHYSICS_SOLVER_ITERATIONS 6

typedef struct {
    Ball balls[PHYSICS_MAX_BALLS];
    int32_t ball_count;

    Bounds3D bounds;
    Vec3 gravity;

    Vec3 mouse_position;
    float mouse_radius;
    float mouse_strength;
    bool mouse_active;

    float damping;
    float restitution;
    float friction;
    float floor_friction;

    float min_velocity;
    float max_velocity;
} PhysicsWorld;

PhysicsWorld* physics_world_create(void);
void physics_world_destroy(PhysicsWorld* world);
void physics_world_set_bounds(PhysicsWorld* world, Bounds3D bounds);
void physics_world_set_mouse(PhysicsWorld* world, Vec3 position, bool active);
int32_t physics_world_add_ball(PhysicsWorld* world, Vec3 position, float radius, Vec3 color);
void physics_world_update(PhysicsWorld* world, float dt);

#ifdef __cplusplus
}
#endif

#endif
