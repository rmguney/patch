#include "collision_object.h"
#include "engine/core/spatial_hash.h"
#include <string.h>

static void get_obb_corners(VoxelObject *obj, Vec3 corners[8])
{
    Vec3 he = obj->shape_half_extents;
    float mat3[9];
    quat_to_mat3(obj->orientation, mat3);

    Vec3 axis_x = vec3_create(mat3[0], mat3[3], mat3[6]);
    Vec3 axis_y = vec3_create(mat3[1], mat3[4], mat3[7]);
    Vec3 axis_z = vec3_create(mat3[2], mat3[5], mat3[8]);

    Vec3 scaled_x = vec3_scale(axis_x, he.x);
    Vec3 scaled_y = vec3_scale(axis_y, he.y);
    Vec3 scaled_z = vec3_scale(axis_z, he.z);

    Vec3 c = obj->position;

    corners[0] = vec3_add(c, vec3_add(vec3_add(scaled_x, scaled_y), scaled_z));
    corners[1] = vec3_add(c, vec3_add(vec3_sub(scaled_x, scaled_y), scaled_z));
    corners[2] = vec3_add(c, vec3_sub(vec3_add(scaled_x, scaled_y), scaled_z));
    corners[3] = vec3_add(c, vec3_sub(vec3_sub(scaled_x, scaled_y), scaled_z));
    corners[4] = vec3_add(c, vec3_add(vec3_add(vec3_neg(scaled_x), scaled_y), scaled_z));
    corners[5] = vec3_add(c, vec3_add(vec3_sub(vec3_neg(scaled_x), scaled_y), scaled_z));
    corners[6] = vec3_add(c, vec3_sub(vec3_add(vec3_neg(scaled_x), scaled_y), scaled_z));
    corners[7] = vec3_add(c, vec3_sub(vec3_sub(vec3_neg(scaled_x), scaled_y), scaled_z));
}

static Vec3 world_to_local(VoxelObject *obj, Vec3 world_point)
{
    Vec3 relative = vec3_sub(world_point, obj->position);
    Quat inv_orient = quat_conjugate(obj->orientation);
    return quat_rotate_vec3(inv_orient, relative);
}

static bool point_in_obb(VoxelObject *obj, Vec3 world_point)
{
    Vec3 local = world_to_local(obj, world_point);
    Vec3 he = obj->shape_half_extents;

    return (fabsf(local.x) <= he.x &&
            fabsf(local.y) <= he.y &&
            fabsf(local.z) <= he.z);
}

static float point_obb_penetration(VoxelObject *obj, Vec3 world_point, Vec3 *out_normal)
{
    Vec3 local = world_to_local(obj, world_point);
    Vec3 he = obj->shape_half_extents;

    float dx = he.x - fabsf(local.x);
    float dy = he.y - fabsf(local.y);
    float dz = he.z - fabsf(local.z);

    if (dx < 0 || dy < 0 || dz < 0)
    {
        *out_normal = vec3_zero();
        return 0.0f;
    }

    float mat3[9];
    quat_to_mat3(obj->orientation, mat3);

    if (dx <= dy && dx <= dz)
    {
        float sign = local.x >= 0.0f ? 1.0f : -1.0f;
        *out_normal = vec3_create(mat3[0] * sign, mat3[3] * sign, mat3[6] * sign);
        return dx;
    }
    else if (dy <= dx && dy <= dz)
    {
        float sign = local.y >= 0.0f ? 1.0f : -1.0f;
        *out_normal = vec3_create(mat3[1] * sign, mat3[4] * sign, mat3[7] * sign);
        return dy;
    }
    else
    {
        float sign = local.z >= 0.0f ? 1.0f : -1.0f;
        *out_normal = vec3_create(mat3[2] * sign, mat3[5] * sign, mat3[8] * sign);
        return dz;
    }
}

static bool test_obb_obb_coarse(VoxelObject *a, VoxelObject *b)
{
    Vec3 delta = vec3_sub(b->position, a->position);
    float dist_sq = vec3_length_sq(delta);
    float combined_radius = a->radius + b->radius;
    return dist_sq <= combined_radius * combined_radius;
}

static bool detect_obb_collision(VoxelObject *obj_a, VoxelObject *obj_b,
                                  Vec3 *out_contact, Vec3 *out_normal, float *out_penetration)
{
    Vec3 corners_a[8], corners_b[8];
    get_obb_corners(obj_a, corners_a);
    get_obb_corners(obj_b, corners_b);

    Vec3 best_contact = vec3_zero();
    Vec3 best_normal = vec3_zero();
    float best_penetration = 0.0f;
    int32_t contact_count = 0;

    for (int32_t i = 0; i < 8; i++)
    {
        if (point_in_obb(obj_b, corners_a[i]))
        {
            Vec3 normal;
            float pen = point_obb_penetration(obj_b, corners_a[i], &normal);
            if (pen > best_penetration)
            {
                best_penetration = pen;
                best_contact = corners_a[i];
                best_normal = normal;
            }
            contact_count++;
        }
    }

    for (int32_t i = 0; i < 8; i++)
    {
        if (point_in_obb(obj_a, corners_b[i]))
        {
            Vec3 normal;
            float pen = point_obb_penetration(obj_a, corners_b[i], &normal);
            if (pen > best_penetration)
            {
                best_penetration = pen;
                best_contact = corners_b[i];
                best_normal = vec3_neg(normal);
            }
            contact_count++;
        }
    }

    if (contact_count > 0 && best_penetration > K_EPSILON)
    {
        *out_contact = best_contact;
        *out_normal = best_normal;
        *out_penetration = best_penetration;
        return true;
    }

    return false;
}

int32_t physics_detect_object_pairs(PhysicsWorld *world,
                                    ObjectCollisionPair *pairs,
                                    int32_t max_pairs)
{
    if (!world || !world->objects || !pairs)
        return 0;

    int32_t pair_count = 0;
    VoxelObjectWorld *obj_world = world->objects;

    for (int32_t i = 0; i < PHYS_MAX_BODIES && pair_count < max_pairs; i++)
    {
        RigidBody *body_a = &world->bodies[i];
        uint8_t flags_a = body_a->flags;
        if (!(flags_a & PHYS_FLAG_ACTIVE) || (flags_a & PHYS_FLAG_SLEEPING))
            continue;

        VoxelObject *obj_a = &obj_world->objects[body_a->vobj_index];
        if (!obj_a->active)
            continue;

        for (int32_t j = i + 1; j < PHYS_MAX_BODIES && pair_count < max_pairs; j++)
        {
            RigidBody *body_b = &world->bodies[j];
            uint8_t flags_b = body_b->flags;
            if (!(flags_b & PHYS_FLAG_ACTIVE))
                continue;

            if ((flags_a & PHYS_FLAG_SLEEPING) && (flags_b & PHYS_FLAG_SLEEPING))
                continue;

            VoxelObject *obj_b = &obj_world->objects[body_b->vobj_index];
            if (!obj_b->active)
                continue;

            if (!test_obb_obb_coarse(obj_a, obj_b))
                continue;

            Vec3 contact, normal;
            float penetration;

            if (detect_obb_collision(obj_a, obj_b, &contact, &normal, &penetration))
            {
                pairs[pair_count].body_a = i;
                pairs[pair_count].body_b = j;
                pairs[pair_count].contact_point = contact;
                pairs[pair_count].contact_normal = normal;
                pairs[pair_count].penetration = penetration;
                pairs[pair_count].valid = true;
                pair_count++;
            }
        }
    }

    return pair_count;
}

static float compute_effective_mass_pair(RigidBody *body, VoxelObject *obj, Vec3 r, Vec3 n)
{
    if (body->inv_mass == 0.0f)
        return 0.0f;

    Vec3 r_cross_n = vec3_cross(r, n);

    float mat3[9];
    quat_to_mat3(obj->orientation, mat3);
    Vec3 rot_mat[3];
    rot_mat[0] = vec3_create(mat3[0], mat3[1], mat3[2]);
    rot_mat[1] = vec3_create(mat3[3], mat3[4], mat3[5]);
    rot_mat[2] = vec3_create(mat3[6], mat3[7], mat3[8]);

    Vec3 local_r_cross_n = vec3_create(
        vec3_dot(rot_mat[0], r_cross_n),
        vec3_dot(rot_mat[1], r_cross_n),
        vec3_dot(rot_mat[2], r_cross_n));

    Vec3 scaled = vec3_mul(local_r_cross_n, body->inv_inertia_local);

    Vec3 world_scaled = vec3_create(
        rot_mat[0].x * scaled.x + rot_mat[1].x * scaled.y + rot_mat[2].x * scaled.z,
        rot_mat[0].y * scaled.x + rot_mat[1].y * scaled.y + rot_mat[2].y * scaled.z,
        rot_mat[0].z * scaled.x + rot_mat[1].z * scaled.y + rot_mat[2].z * scaled.z);

    Vec3 term = vec3_cross(world_scaled, r);
    return body->inv_mass + vec3_dot(term, n);
}

static Vec3 get_point_vel(RigidBody *body, VoxelObject *obj, Vec3 world_point)
{
    Vec3 r = vec3_sub(world_point, obj->position);
    return vec3_add(body->velocity, vec3_cross(body->angular_velocity, r));
}

void physics_resolve_object_collision(PhysicsWorld *world,
                                      ObjectCollisionPair *pair,
                                      float dt)
{
    if (!world || !pair || !pair->valid)
        return;

    RigidBody *body_a = &world->bodies[pair->body_a];
    RigidBody *body_b = &world->bodies[pair->body_b];

    if (!(body_a->flags & PHYS_FLAG_ACTIVE) || !(body_b->flags & PHYS_FLAG_ACTIVE))
        return;

    VoxelObject *obj_a = &world->objects->objects[body_a->vobj_index];
    VoxelObject *obj_b = &world->objects->objects[body_b->vobj_index];

    Vec3 r_a = vec3_sub(pair->contact_point, obj_a->position);
    Vec3 r_b = vec3_sub(pair->contact_point, obj_b->position);
    Vec3 n = pair->contact_normal;

    Vec3 vel_a = get_point_vel(body_a, obj_a, pair->contact_point);
    Vec3 vel_b = get_point_vel(body_b, obj_b, pair->contact_point);
    Vec3 rel_vel = vec3_sub(vel_a, vel_b);

    float v_n = vec3_dot(rel_vel, n);

    if (v_n > 0.0f)
        return;

    float eff_mass_a = compute_effective_mass_pair(body_a, obj_a, r_a, n);
    float eff_mass_b = compute_effective_mass_pair(body_b, obj_b, r_b, n);
    float total_eff_mass = eff_mass_a + eff_mass_b;

    if (total_eff_mass < K_EPSILON)
        return;

    float e = minf(body_a->restitution, body_b->restitution);
    float bias = -PHYS_BAUMGARTE_FACTOR * (1.0f / dt) * maxf(0.0f, pair->penetration - PHYS_SLOP);

    float j_n = (-(1.0f + e) * v_n + bias) / total_eff_mass;
    if (j_n < 0.0f)
        j_n = 0.0f;

    Vec3 impulse_n = vec3_scale(n, j_n);
    physics_body_apply_impulse(world, pair->body_a, impulse_n, pair->contact_point);
    physics_body_apply_impulse(world, pair->body_b, vec3_neg(impulse_n), pair->contact_point);

    Vec3 tangent = vec3_sub(rel_vel, vec3_scale(n, v_n));
    float tangent_len = vec3_length(tangent);

    if (tangent_len > K_EPSILON)
    {
        tangent = vec3_scale(tangent, 1.0f / tangent_len);
        float v_t = tangent_len;
        float mu = (body_a->friction + body_b->friction) * 0.5f;
        float j_t = -v_t / total_eff_mass;
        float max_friction = mu * j_n;

        if (j_t < -max_friction)
            j_t = -max_friction;
        if (j_t > max_friction)
            j_t = max_friction;

        Vec3 impulse_t = vec3_scale(tangent, j_t);
        physics_body_apply_impulse(world, pair->body_a, impulse_t, pair->contact_point);
        physics_body_apply_impulse(world, pair->body_b, vec3_neg(impulse_t), pair->contact_point);
    }

    float separation = pair->penetration * 0.5f;
    if (body_a->inv_mass > 0.0f)
        obj_a->position = vec3_add(obj_a->position, vec3_scale(n, separation * body_a->inv_mass / (body_a->inv_mass + body_b->inv_mass)));
    if (body_b->inv_mass > 0.0f)
        obj_b->position = vec3_sub(obj_b->position, vec3_scale(n, separation * body_b->inv_mass / (body_a->inv_mass + body_b->inv_mass)));

    physics_body_wake(world, pair->body_a);
    physics_body_wake(world, pair->body_b);
}

void physics_process_object_collisions(PhysicsWorld *world, float dt)
{
    if (!world)
        return;

    ObjectCollisionPair pairs[PHYS_OBJ_COLLISION_BUDGET];
    int32_t pair_count = physics_detect_object_pairs(world, pairs, PHYS_OBJ_COLLISION_BUDGET);

    for (int32_t i = 0; i < pair_count; i++)
    {
        physics_resolve_object_collision(world, &pairs[i], dt);
    }
}
