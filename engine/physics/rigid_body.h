#ifndef PATCH_PHYSICS_RIGID_BODY_H
#define PATCH_PHYSICS_RIGID_BODY_H

#include "engine/core/types.h"
#include "engine/core/math.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct VoxelObject VoxelObject;

    void rigid_body_integrate_velocity(VoxelObject *obj, Vec3 gravity, float dt,
                                       float linear_damping, float angular_damping);

    void rigid_body_integrate_position(VoxelObject *obj, float dt);

    void rigid_body_update_inertia(VoxelObject *obj);

    void rigid_body_compute_inertia(VoxelObject *obj);

    void rigid_body_apply_impulse(VoxelObject *obj, Vec3 impulse, Vec3 contact_point);

    void rigid_body_apply_torque_impulse(VoxelObject *obj, Vec3 torque);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_PHYSICS_RIGID_BODY_H */
