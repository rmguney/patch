#include "rigid_body.h"
#include "engine/sim/voxel_object.h"
#include <string.h>

void rigid_body_integrate_velocity(VoxelObject *obj, Vec3 gravity, float dt,
                                   float linear_damping, float angular_damping)
{
    if (!obj || obj->inv_mass == 0.0f)
        return;

    obj->velocity = vec3_add(obj->velocity, vec3_scale(gravity, dt));

    float linear_factor = 1.0f / (1.0f + dt * linear_damping);
    float angular_factor = 1.0f / (1.0f + dt * angular_damping);

    obj->velocity = vec3_scale(obj->velocity, linear_factor);
    obj->angular_velocity = vec3_scale(obj->angular_velocity, angular_factor);
}

void rigid_body_integrate_position(VoxelObject *obj, float dt)
{
    if (!obj)
        return;

    obj->position = vec3_add(obj->position, vec3_scale(obj->velocity, dt));

    quat_integrate(&obj->orientation, obj->angular_velocity, dt);
    obj->orientation = quat_normalize(obj->orientation);
}

void rigid_body_update_inertia(VoxelObject *obj)
{
    if (!obj)
        return;

    float rot[9];
    quat_to_mat3(obj->orientation, rot);

    float rot_t[9];
    mat3_transpose(rot, rot_t);

    float temp[9];
    mat3_multiply(rot, obj->inv_inertia_local, temp);
    mat3_multiply(temp, rot_t, obj->inv_inertia_world);
}

void rigid_body_compute_inertia(VoxelObject *obj)
{
    if (!obj || obj->mass <= 0.0f)
        return;

    obj->inv_mass = 1.0f / obj->mass;

    Vec3 half = obj->shape_half_extents;
    float w = half.x * 2.0f;
    float h = half.y * 2.0f;
    float d = half.z * 2.0f;

    float i_xx = (obj->mass / 12.0f) * (h * h + d * d);
    float i_yy = (obj->mass / 12.0f) * (w * w + d * d);
    float i_zz = (obj->mass / 12.0f) * (w * w + h * h);

    float min_inertia = 0.001f;
    if (i_xx < min_inertia) i_xx = min_inertia;
    if (i_yy < min_inertia) i_yy = min_inertia;
    if (i_zz < min_inertia) i_zz = min_inertia;

    memset(obj->inv_inertia_local, 0, sizeof(obj->inv_inertia_local));
    obj->inv_inertia_local[0] = 1.0f / i_xx;
    obj->inv_inertia_local[4] = 1.0f / i_yy;
    obj->inv_inertia_local[8] = 1.0f / i_zz;

    rigid_body_update_inertia(obj);
}

void rigid_body_apply_impulse(VoxelObject *obj, Vec3 impulse, Vec3 contact_point)
{
    if (!obj || obj->inv_mass == 0.0f)
        return;

    obj->velocity = vec3_add(obj->velocity, vec3_scale(impulse, obj->inv_mass));

    Vec3 com_world = vec3_add(obj->position, obj->center_of_mass_offset);
    Vec3 r = vec3_sub(contact_point, com_world);
    Vec3 torque = vec3_cross(r, impulse);

    Vec3 angular_impulse = mat3_transform_vec3(obj->inv_inertia_world, torque);
    obj->angular_velocity = vec3_add(obj->angular_velocity, angular_impulse);
}

void rigid_body_apply_torque_impulse(VoxelObject *obj, Vec3 torque)
{
    if (!obj)
        return;

    Vec3 angular_impulse = mat3_transform_vec3(obj->inv_inertia_world, torque);
    obj->angular_velocity = vec3_add(obj->angular_velocity, angular_impulse);
}
