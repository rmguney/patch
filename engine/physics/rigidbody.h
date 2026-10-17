#ifndef PATCH_PHYSICS_RIGIDBODY_H
#define PATCH_PHYSICS_RIGIDBODY_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/voxel_object.h"
#include "broadphase.h"
#include "convex_hull.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define PHYS_MAX_BODIES 512
#define PHYS_GRAVITY_Y (-18.0f)
/* Damping factors are per 1/PHYS_DAMPING_REFERENCE_HZ seconds, so they do not depend
 * on the step size or the substep count */
#define PHYS_DAMPING_REFERENCE_HZ 60.0f
#define PHYS_LINEAR_DAMPING 0.99f
#define PHYS_ANGULAR_DAMPING 0.98f
#define PHYS_GROUND_LINEAR_DAMPING 0.90f
#define PHYS_GROUND_ANGULAR_DAMPING 0.85f
#define PHYS_MAX_LINEAR_VELOCITY 30.0f
#define PHYS_MAX_ANGULAR_VELOCITY 20.0f
#define PHYS_SLEEP_LINEAR_THRESHOLD 0.05f
#define PHYS_SLEEP_ANGULAR_THRESHOLD 0.1f
#define PHYS_SLEEP_FRAMES 30
#define PHYS_SETTLE_LINEAR_THRESHOLD 0.3f
#define PHYS_SETTLE_ANGULAR_THRESHOLD 0.4f
#define PHYS_GROUND_PERSIST_FRAMES 5
#define PHYS_DEFAULT_RESTITUTION 0.4f
#define PHYS_BOUNCE_DEAD_ZONE 0.2f
#define PHYS_DEFAULT_FRICTION 0.5f
#define PHYS_BAUMGARTE_FACTOR 0.2f
#define PHYS_SLOP 0.005f
#define PHYS_TERRAIN_SAMPLE_POINTS 14
#define PHYS_SUBSTEP_VELOCITY_THRESHOLD 10.0f
#define PHYS_MAX_SUBSTEPS 4
#define PHYS_STABLE_SUPPORT_RATIO 0.8f

#define PHYS_FLAG_ACTIVE   (1 << 0)
#define PHYS_FLAG_SLEEPING (1 << 1)
#define PHYS_FLAG_STATIC   (1 << 2) /* Immovable: not integrated, infinite mass in contacts */
#define PHYS_FLAG_GROUNDED (1 << 3) /* Resting on terrain (persists PHYS_GROUND_PERSIST_FRAMES) */
#define PHYS_FLAG_STABLE   (1 << 4) /* Centre of mass over its terrain support; may sleep */

    typedef struct
    {
        int32_t vobj_index;
        uint32_t vobj_alloc_id; /* alloc_id of the object the body was created for */
        Vec3 velocity;         /* Of the centre of mass */
        Vec3 angular_velocity; /* World space */
        Vec3 com;              /* World centre of mass the velocities refer to, as of the last step or sync */
        float inv_mass;
        Vec3 inv_inertia_local; /* Inverse of the diagonal inertia about the centre of mass, object axes */
        float restitution;
        float friction;
        uint8_t sleep_frames;
        uint8_t ground_frames;
        uint8_t flags;
        int16_t next_free;
        uint32_t synced_revision; /* voxel_revision the mass properties were taken from */
    } RigidBody;

    /* Convex hull of one object slot, built lazily for collision and rebuilt whenever the
     * object's voxel_revision changes */
    typedef struct
    {
        ConvexHull hull;
        uint32_t revision; /* 0 = never built (revisions are never zero) */
    } PhysicsHullCache;

    typedef struct PhysicsWorld
    {
        RigidBody bodies[PHYS_MAX_BODIES];
        int32_t first_free;
        int32_t max_body_index;
        int16_t vobj_to_body[VOBJ_MAX_OBJECTS];
        VoxelObjectWorld *objects;
        VoxelVolume *terrain;
        Vec3 gravity;
        SAPBroadphase *broadphase;
        PhysicsHullCache *hull_cache; /* VOBJ_MAX_OBJECTS entries */
        uint32_t terrain_removals_seen; /* Entries of terrain->removal_log handled so far */
    } PhysicsWorld;

    PhysicsWorld *physics_world_create(VoxelObjectWorld *objects, VoxelVolume *terrain);
    void physics_world_destroy(PhysicsWorld *world);

    /* Bodies follow the object world through physics_world_sync_objects; adding one by
     * hand is for objects that should simulate before the next sync. */
    int32_t physics_world_add_body(PhysicsWorld *world, int32_t vobj_index);
    int32_t physics_world_find_body_for_object(PhysicsWorld *world, int32_t vobj_index);
    RigidBody *physics_world_get_body(PhysicsWorld *world, int32_t body_index);

    void physics_world_step(PhysicsWorld *world, float dt);

    /* Adds bodies for new objects (split pieces take over their parent's motion), drops
     * bodies of freed objects and re-reads mass properties after voxel edits */
    void physics_world_sync_objects(PhysicsWorld *world);

    void physics_body_wake(PhysicsWorld *world, int32_t body_index);
    void physics_body_set_velocity(PhysicsWorld *world, int32_t body_index, Vec3 velocity);
    void physics_body_set_angular_velocity(PhysicsWorld *world, int32_t body_index, Vec3 angular_velocity);

    /* Helpers shared by the integrator and the terrain and object solvers. r is a
     * contact point relative to the body's centre of mass. */

    /* Integrated and pushed by contacts (active, not static, not sleeping) */
    static inline bool rigid_body_is_dynamic(const RigidBody *body)
    {
        return (body->flags & PHYS_FLAG_ACTIVE) && !(body->flags & (PHYS_FLAG_STATIC | PHYS_FLAG_SLEEPING));
    }

    /* World centre of mass; obj->position is the grid centre */
    static inline Vec3 physics_object_com(const VoxelObject *obj)
    {
        return vec3_add(obj->position, quat_rotate_vec3(obj->orientation, obj->local_com));
    }

    static inline Vec3 rigid_body_point_velocity(const RigidBody *body, Vec3 r)
    {
        return vec3_add(body->velocity, vec3_cross(body->angular_velocity, r));
    }

    /* Velocity change at the contact, along unit dir, per unit impulse along dir:
     * 1/m + dir . ((I^-1 (r x dir)) x r) */
    static inline float rigid_body_inv_mass_along(const RigidBody *body, Quat orientation, Vec3 r, Vec3 dir)
    {
        Vec3 ang = quat_apply_inv_inertia(orientation, body->inv_inertia_local, vec3_cross(r, dir));
        return body->inv_mass + vec3_dot(vec3_cross(ang, r), dir);
    }

    static inline void rigid_body_add_impulse(RigidBody *body, Quat orientation, Vec3 impulse, Vec3 r)
    {
        body->velocity = vec3_add(body->velocity, vec3_scale(impulse, body->inv_mass));
        body->angular_velocity = vec3_add(body->angular_velocity,
                                          quat_apply_inv_inertia(orientation, body->inv_inertia_local,
                                                                 vec3_cross(r, impulse)));
    }

#ifdef __cplusplus
}
#endif

#endif
