#ifndef PATCH_PHYSICS_RIGIDBODY_H
#define PATCH_PHYSICS_RIGIDBODY_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/voxel_object.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define PHYS_MAX_BODIES 512
#define PHYS_GRAVITY_Y -18.0f
#define PHYS_LINEAR_DAMPING 0.99f
#define PHYS_ANGULAR_DAMPING 0.98f
#define PHYS_MAX_LINEAR_VELOCITY 30.0f
#define PHYS_MAX_ANGULAR_VELOCITY 20.0f
#define PHYS_SLEEP_LINEAR_THRESHOLD 0.05f
#define PHYS_SLEEP_ANGULAR_THRESHOLD 0.1f
#define PHYS_SLEEP_FRAMES 30
#define PHYS_GROUND_LINEAR_DAMPING 0.85f
#define PHYS_GROUND_ANGULAR_DAMPING 0.80f
#define PHYS_SETTLE_LINEAR_THRESHOLD 0.3f
#define PHYS_SETTLE_ANGULAR_THRESHOLD 0.4f
#define PHYS_GROUND_PERSIST_FRAMES 5
#define PHYS_DEFAULT_RESTITUTION 0.3f
#define PHYS_DEFAULT_FRICTION 0.5f
#define PHYS_VOXEL_DENSITY 1.0f
#define PHYS_BAUMGARTE_FACTOR 0.2f
#define PHYS_SLOP 0.005f
#define PHYS_MAX_COLLISION_PAIRS 128
#define PHYS_TERRAIN_SAMPLE_POINTS 14

#define PHYS_FLAG_ACTIVE     (1 << 0)
#define PHYS_FLAG_SLEEPING   (1 << 1)
#define PHYS_FLAG_STATIC     (1 << 2)
#define PHYS_FLAG_KINEMATIC  (1 << 3)
#define PHYS_FLAG_GROUNDED   (1 << 4)

    typedef struct
    {
        int32_t vobj_index;
        Vec3 velocity;
        Vec3 angular_velocity;
        float mass;
        float inv_mass;
        Vec3 inertia_local;
        Vec3 inv_inertia_local;
        float restitution;
        float friction;
        uint8_t sleep_frames;
        uint8_t ground_frames;
        uint8_t flags;
        int16_t next_free;
    } RigidBody;

    typedef struct
    {
        int32_t body_a;
        int32_t body_b;
        Vec3 contact_point;
        Vec3 contact_normal;
        float penetration;
    } CollisionPair;

    typedef struct PhysicsWorld
    {
        RigidBody bodies[PHYS_MAX_BODIES];
        int32_t body_count;
        int32_t first_free;
        int32_t max_body_index;
        int16_t vobj_to_body[VOBJ_MAX_OBJECTS];
        VoxelObjectWorld *objects;
        VoxelVolume *terrain;
        Vec3 gravity;
        CollisionPair collision_pairs[PHYS_MAX_COLLISION_PAIRS];
        int32_t collision_pair_count;
    } PhysicsWorld;

    PhysicsWorld *physics_world_create(VoxelObjectWorld *objects, VoxelVolume *terrain);
    void physics_world_destroy(PhysicsWorld *world);

    int32_t physics_world_add_body(PhysicsWorld *world, int32_t vobj_index);
    int32_t physics_world_add_body_with_mass(PhysicsWorld *world, int32_t vobj_index,
                                              float mass, Vec3 half_extents);
    void physics_world_remove_body(PhysicsWorld *world, int32_t body_index);
    int32_t physics_world_find_body_for_object(PhysicsWorld *world, int32_t vobj_index);

    void physics_world_step(PhysicsWorld *world, float dt);

    RigidBody *physics_world_get_body(PhysicsWorld *world, int32_t body_index);
    int32_t physics_world_get_body_count(PhysicsWorld *world);
    bool physics_body_is_sleeping(PhysicsWorld *world, int32_t body_index);

    void physics_body_apply_impulse(PhysicsWorld *world, int32_t body_index, Vec3 impulse, Vec3 world_point);
    void physics_body_apply_force(PhysicsWorld *world, int32_t body_index, Vec3 force);
    void physics_body_apply_torque(PhysicsWorld *world, int32_t body_index, Vec3 torque);
    void physics_body_wake(PhysicsWorld *world, int32_t body_index);

    void physics_body_set_velocity(PhysicsWorld *world, int32_t body_index, Vec3 velocity);
    void physics_body_set_angular_velocity(PhysicsWorld *world, int32_t body_index, Vec3 angular_velocity);

    void physics_body_compute_inertia(RigidBody *body, Vec3 half_extents);

    void physics_world_sync_objects(PhysicsWorld *world);

#ifdef __cplusplus
}
#endif

#endif
