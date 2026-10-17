#ifndef PATCH_PHYSICS_COLLISION_OBJECT_H
#define PATCH_PHYSICS_COLLISION_OBJECT_H

#include "rigidbody.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PHYS_OBJ_COLLISION_BUDGET 512
#define PHYS_HULL_MARGIN 0.04f

    typedef struct
    {
        int32_t body_a;
        int32_t body_b;
        Vec3 contact_point;
        Vec3 contact_normal; /* Unit vector from A towards B */
        float penetration;
    } ObjectCollisionPair;

    /* Contacts between overlapping object hulls (GJK + EPA), for pairs from the
     * broadphase where at least one body can move */
    int32_t physics_detect_object_pairs(PhysicsWorld *world, ObjectCollisionPair *pairs, int32_t max_pairs);

    /* Detects and resolves object-object contacts for one (sub)step */
    void physics_process_object_collisions(PhysicsWorld *world);

#ifdef __cplusplus
}
#endif

#endif
