#ifndef PATCH_PHYSICS_COLLISION_OBJECT_H
#define PATCH_PHYSICS_COLLISION_OBJECT_H

#include "rigidbody.h"
#include "engine/core/types.h"
#include "engine/voxel/voxel_object.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PHYS_OBJ_COLLISION_BUDGET 128
#define PHYS_OBJ_SAMPLE_POINTS 8

    typedef struct
    {
        int32_t body_a;
        int32_t body_b;
        Vec3 contact_point;
        Vec3 contact_normal;
        float penetration;
        bool valid;
    } ObjectCollisionPair;

    int32_t physics_detect_object_pairs(PhysicsWorld *world,
                                        ObjectCollisionPair *pairs,
                                        int32_t max_pairs);

    void physics_resolve_object_collision(PhysicsWorld *world,
                                          ObjectCollisionPair *pair,
                                          float dt);

    void physics_process_object_collisions(PhysicsWorld *world, float dt);

#ifdef __cplusplus
}
#endif

#endif
