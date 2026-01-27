#include "rigidbody.h"
#include "collision_object.h"
#include "content/materials.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>

static Vec3 vobj_world_com(VoxelObject *obj);

PhysicsWorld *physics_world_create(VoxelObjectWorld *objects, VoxelVolume *terrain)
{
    PhysicsWorld *world = (PhysicsWorld *)calloc(1, sizeof(PhysicsWorld));
    if (!world)
        return NULL;

    world->objects = objects;
    world->terrain = terrain;
    world->gravity = vec3_create(0.0f, PHYS_GRAVITY_Y, 0.0f);
    world->body_count = 0;
    world->first_free = -1;
    world->max_body_index = -1;

    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
        world->vobj_to_body[i] = -1;

    for (int32_t i = 0; i < PHYS_MAX_BODIES; i++)
    {
        world->bodies[i].flags = 0;
        world->bodies[i].next_free = -1;
    }

    return world;
}

void physics_world_destroy(PhysicsWorld *world)
{
    if (world)
        free(world);
}

static int32_t find_free_slot(PhysicsWorld *world)
{
    if (world->first_free >= 0)
    {
        int32_t slot = world->first_free;
        world->first_free = world->bodies[slot].next_free;
        return slot;
    }

    for (int32_t i = 0; i < PHYS_MAX_BODIES; i++)
    {
        if (!(world->bodies[i].flags & PHYS_FLAG_ACTIVE))
            return i;
    }
    return -1;
}

void physics_body_compute_inertia(RigidBody *body, Vec3 half_extents)
{
    float w = half_extents.x * 2.0f;
    float h = half_extents.y * 2.0f;
    float d = half_extents.z * 2.0f;
    float m = body->mass;
    float factor = m / 12.0f;

    body->inertia_local.x = factor * (h * h + d * d);
    body->inertia_local.y = factor * (w * w + d * d);
    body->inertia_local.z = factor * (w * w + h * h);

    if (body->inertia_local.x > K_EPSILON)
        body->inv_inertia_local.x = 1.0f / body->inertia_local.x;
    if (body->inertia_local.y > K_EPSILON)
        body->inv_inertia_local.y = 1.0f / body->inertia_local.y;
    if (body->inertia_local.z > K_EPSILON)
        body->inv_inertia_local.z = 1.0f / body->inertia_local.z;
}

int32_t physics_world_add_body(PhysicsWorld *world, int32_t vobj_index)
{
    if (!world || !world->objects)
        return -1;

    if (vobj_index < 0 || vobj_index >= VOBJ_MAX_OBJECTS)
        return -1;

    VoxelObject *obj = &world->objects->objects[vobj_index];
    if (!obj->active)
        return -1;

    int32_t slot = find_free_slot(world);
    if (slot < 0)
        return -1;

    RigidBody *body = &world->bodies[slot];
    memset(body, 0, sizeof(RigidBody));

    body->vobj_index = vobj_index;
    body->velocity = vec3_zero();
    body->angular_velocity = vec3_zero();

    body->mass = obj->total_mass > K_EPSILON ? obj->total_mass
                                             : (float)obj->voxel_count * PHYS_VOXEL_DENSITY;
    if (body->mass < K_EPSILON)
        body->mass = K_EPSILON;
    body->inv_mass = 1.0f / body->mass;

    if (obj->inertia_diag.x > K_EPSILON && obj->inertia_diag.y > K_EPSILON &&
        obj->inertia_diag.z > K_EPSILON)
    {
        body->inertia_local = obj->inertia_diag;
        body->inv_inertia_local = vec3_create(
            1.0f / obj->inertia_diag.x,
            1.0f / obj->inertia_diag.y,
            1.0f / obj->inertia_diag.z);
    }
    else
    {
        physics_body_compute_inertia(body, obj->shape_half_extents);
    }

    body->restitution = PHYS_DEFAULT_RESTITUTION;
    body->friction = PHYS_DEFAULT_FRICTION;
    body->sleep_frames = 0;
    body->ground_frames = 0;
    body->flags = PHYS_FLAG_ACTIVE;
    body->next_free = -1;
    body->synced_revision = obj->voxel_revision;

    world->body_count++;
    if (slot > world->max_body_index)
        world->max_body_index = slot;
    world->vobj_to_body[vobj_index] = (int16_t)slot;
    return slot;
}

int32_t physics_world_add_body_with_mass(PhysicsWorld *world, int32_t vobj_index,
                                         float mass, Vec3 half_extents)
{
    if (!world || !world->objects)
        return -1;

    if (vobj_index < 0 || vobj_index >= VOBJ_MAX_OBJECTS)
        return -1;

    VoxelObject *obj = &world->objects->objects[vobj_index];
    if (!obj->active)
        return -1;

    int32_t slot = find_free_slot(world);
    if (slot < 0)
        return -1;

    RigidBody *body = &world->bodies[slot];
    memset(body, 0, sizeof(RigidBody));

    body->vobj_index = vobj_index;
    body->velocity = vec3_zero();
    body->angular_velocity = vec3_zero();

    body->mass = mass;
    if (body->mass < K_EPSILON)
        body->mass = K_EPSILON;
    body->inv_mass = 1.0f / body->mass;

    physics_body_compute_inertia(body, half_extents);

    body->restitution = PHYS_DEFAULT_RESTITUTION;
    body->friction = PHYS_DEFAULT_FRICTION;
    body->sleep_frames = 0;
    body->ground_frames = 0;
    body->flags = PHYS_FLAG_ACTIVE;
    body->next_free = -1;

    world->body_count++;
    if (slot > world->max_body_index)
        world->max_body_index = slot;
    world->vobj_to_body[vobj_index] = (int16_t)slot;
    return slot;
}

void physics_world_remove_body(PhysicsWorld *world, int32_t body_index)
{
    if (!world || body_index < 0 || body_index >= PHYS_MAX_BODIES)
        return;

    RigidBody *body = &world->bodies[body_index];
    if (!(body->flags & PHYS_FLAG_ACTIVE))
        return;

    if (body->vobj_index >= 0 && body->vobj_index < VOBJ_MAX_OBJECTS)
        world->vobj_to_body[body->vobj_index] = -1;

    body->flags = 0;
    body->next_free = world->first_free;
    world->first_free = body_index;
    world->body_count--;
}

int32_t physics_world_find_body_for_object(PhysicsWorld *world, int32_t vobj_index)
{
    if (!world)
        return -1;
    if (vobj_index < 0 || vobj_index >= VOBJ_MAX_OBJECTS)
        return -1;

    int16_t body_idx = world->vobj_to_body[vobj_index];
    if (body_idx >= 0 && (world->bodies[body_idx].flags & PHYS_FLAG_ACTIVE))
        return body_idx;

    return -1;
}

RigidBody *physics_world_get_body(PhysicsWorld *world, int32_t body_index)
{
    if (!world || body_index < 0 || body_index >= PHYS_MAX_BODIES)
        return NULL;
    if (!(world->bodies[body_index].flags & PHYS_FLAG_ACTIVE))
        return NULL;
    return &world->bodies[body_index];
}

int32_t physics_world_get_body_count(PhysicsWorld *world)
{
    return world ? world->body_count : 0;
}

bool physics_body_is_sleeping(PhysicsWorld *world, int32_t body_index)
{
    RigidBody *body = physics_world_get_body(world, body_index);
    return body ? (body->flags & PHYS_FLAG_SLEEPING) != 0 : true;
}

void physics_body_wake(PhysicsWorld *world, int32_t body_index)
{
    RigidBody *body = physics_world_get_body(world, body_index);
    if (body)
    {
        body->flags &= ~PHYS_FLAG_SLEEPING;
        body->sleep_frames = 0;
    }
}

void physics_body_set_velocity(PhysicsWorld *world, int32_t body_index, Vec3 velocity)
{
    RigidBody *body = physics_world_get_body(world, body_index);
    if (body)
    {
        body->velocity = velocity;
        physics_body_wake(world, body_index);
    }
}

void physics_body_set_angular_velocity(PhysicsWorld *world, int32_t body_index, Vec3 angular_velocity)
{
    RigidBody *body = physics_world_get_body(world, body_index);
    if (body)
    {
        body->angular_velocity = angular_velocity;
        physics_body_wake(world, body_index);
    }
}

void physics_body_apply_impulse(PhysicsWorld *world, int32_t body_index, Vec3 impulse, Vec3 world_point)
{
    RigidBody *body = physics_world_get_body(world, body_index);
    if (!body || body->inv_mass == 0.0f)
        return;

    float impulse_mag = vec3_length(impulse);
    if (impulse_mag < 0.001f)
        return;

    VoxelObject *obj = &world->objects->objects[body->vobj_index];
    Vec3 r = vec3_sub(world_point, vobj_world_com(obj));

    body->velocity = vec3_add(body->velocity, vec3_scale(impulse, body->inv_mass));

    Vec3 angular_impulse = vec3_cross(r, impulse);
    Vec3 rot_mat[3];
    float mat3[9];
    quat_to_mat3(obj->orientation, mat3);
    rot_mat[0] = vec3_create(mat3[0], mat3[1], mat3[2]);
    rot_mat[1] = vec3_create(mat3[3], mat3[4], mat3[5]);
    rot_mat[2] = vec3_create(mat3[6], mat3[7], mat3[8]);

    Vec3 local_angular = vec3_create(
        vec3_dot(rot_mat[0], angular_impulse),
        vec3_dot(rot_mat[1], angular_impulse),
        vec3_dot(rot_mat[2], angular_impulse));

    Vec3 delta_angular = vec3_mul(local_angular, body->inv_inertia_local);

    Vec3 world_delta = vec3_create(
        rot_mat[0].x * delta_angular.x + rot_mat[1].x * delta_angular.y + rot_mat[2].x * delta_angular.z,
        rot_mat[0].y * delta_angular.x + rot_mat[1].y * delta_angular.y + rot_mat[2].y * delta_angular.z,
        rot_mat[0].z * delta_angular.x + rot_mat[1].z * delta_angular.y + rot_mat[2].z * delta_angular.z);

    body->angular_velocity = vec3_add(body->angular_velocity, world_delta);

    if (impulse_mag > 0.1f)
        physics_body_wake(world, body_index);
}

void physics_body_apply_force(PhysicsWorld *world, int32_t body_index, Vec3 force)
{
    (void)world;
    (void)body_index;
    (void)force;
}

void physics_body_apply_torque(PhysicsWorld *world, int32_t body_index, Vec3 torque)
{
    (void)world;
    (void)body_index;
    (void)torque;
}

static void get_obb_sample_points(VoxelObject *obj, Vec3 points[PHYS_TERRAIN_SAMPLE_POINTS])
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

    points[0] = vec3_add(c, vec3_add(vec3_add(scaled_x, scaled_y), scaled_z));
    points[1] = vec3_add(c, vec3_add(vec3_sub(scaled_x, scaled_y), scaled_z));
    points[2] = vec3_add(c, vec3_sub(vec3_add(scaled_x, scaled_y), scaled_z));
    points[3] = vec3_add(c, vec3_sub(vec3_sub(scaled_x, scaled_y), scaled_z));
    points[4] = vec3_add(c, vec3_add(vec3_add(vec3_neg(scaled_x), scaled_y), scaled_z));
    points[5] = vec3_add(c, vec3_add(vec3_sub(vec3_neg(scaled_x), scaled_y), scaled_z));
    points[6] = vec3_add(c, vec3_sub(vec3_add(vec3_neg(scaled_x), scaled_y), scaled_z));
    points[7] = vec3_add(c, vec3_sub(vec3_sub(vec3_neg(scaled_x), scaled_y), scaled_z));

    points[8] = vec3_add(c, scaled_x);
    points[9] = vec3_sub(c, scaled_x);
    points[10] = vec3_add(c, scaled_y);
    points[11] = vec3_sub(c, scaled_y);
    points[12] = vec3_add(c, scaled_z);
    points[13] = vec3_sub(c, scaled_z);
}

#define PHYS_MAX_COMPOUND_POINTS 64

static int32_t get_collider_ground_points(VoxelObject *obj, Vec3 *points, int32_t max_points)
{
    Vec3 c = obj->position;
    int32_t count = 0;

    for (int32_t b = 0; b < obj->collider_box_count && count < max_points; b++)
    {
        ColliderBox *box = &obj->collider_boxes[b];

        Vec3 box_center = vec3_scale(vec3_add(box->local_min, box->local_max), 0.5f);
        Vec3 box_half = vec3_scale(vec3_sub(box->local_max, box->local_min), 0.5f);

        for (int32_t corner = 0; corner < 8 && count < max_points; corner++)
        {
            Vec3 local = vec3_create(
                box_center.x + ((corner & 1) ? box_half.x : -box_half.x),
                box_center.y + ((corner & 2) ? box_half.y : -box_half.y),
                box_center.z + ((corner & 4) ? box_half.z : -box_half.z));

            points[count++] = vec3_add(c, quat_rotate_vec3(obj->orientation, local));
        }
    }

    return count;
}

static Vec3 estimate_terrain_normal(VoxelVolume *terrain, Vec3 point, float probe_dist,
                                    Vec3 obj_center)
{
    float scales[] = {1.0f, 2.0f, 4.0f};
    for (int32_t s = 0; s < 3; s++)
    {
        float pd = probe_dist * scales[s];
        float dx = (volume_get_at(terrain, vec3_create(point.x + pd, point.y, point.z)) != 0 ? 1.0f : 0.0f) -
                   (volume_get_at(terrain, vec3_create(point.x - pd, point.y, point.z)) != 0 ? 1.0f : 0.0f);
        float dy = (volume_get_at(terrain, vec3_create(point.x, point.y + pd, point.z)) != 0 ? 1.0f : 0.0f) -
                   (volume_get_at(terrain, vec3_create(point.x, point.y - pd, point.z)) != 0 ? 1.0f : 0.0f);
        float dz = (volume_get_at(terrain, vec3_create(point.x, point.y, point.z + pd)) != 0 ? 1.0f : 0.0f) -
                   (volume_get_at(terrain, vec3_create(point.x, point.y, point.z - pd)) != 0 ? 1.0f : 0.0f);

        Vec3 gradient = vec3_create(-dx, -dy, -dz);
        float len = vec3_length(gradient);
        if (len > K_EPSILON)
            return vec3_scale(gradient, 1.0f / len);
    }

    Vec3 to_center = vec3_sub(obj_center, point);
    float len = vec3_length(to_center);
    if (len > K_EPSILON)
        return vec3_scale(to_center, 1.0f / len);

    return vec3_zero();
}

static float compute_effective_mass(RigidBody *body, VoxelObject *obj, Vec3 r, Vec3 n)
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
    float angular_term = vec3_dot(term, n);

    return body->inv_mass + angular_term;
}

static Vec3 vobj_world_com(VoxelObject *obj)
{
    Vec3 rotated_com = quat_rotate_vec3(obj->orientation, obj->local_com);
    return vec3_add(obj->position, rotated_com);
}

static Vec3 get_point_velocity(RigidBody *body, VoxelObject *obj, Vec3 world_point)
{
    Vec3 r = vec3_sub(world_point, vobj_world_com(obj));
    return vec3_add(body->velocity, vec3_cross(body->angular_velocity, r));
}

static float estimate_penetration_depth(VoxelVolume *terrain, Vec3 point, Vec3 normal, float voxel_size)
{
    float max_probe = voxel_size * 2.0f;
    float step = voxel_size * 0.25f;

    for (float d = 0.0f; d < max_probe; d += step)
    {
        Vec3 probe = vec3_add(point, vec3_scale(normal, d));
        if (volume_get_at(terrain, probe) == 0)
            return d;
    }
    return max_probe;
}

static void solve_terrain_collision(PhysicsWorld *world, int32_t body_index, float dt)
{
    RigidBody *body = &world->bodies[body_index];
    VoxelObject *obj = &world->objects->objects[body->vobj_index];

    Vec3 sample_points[PHYS_TERRAIN_SAMPLE_POINTS];
    get_obb_sample_points(obj, sample_points);

    float voxel_size = world->terrain->voxel_size;
    float probe_dist = voxel_size * 0.5f;

    float lin_speed = vec3_length(body->velocity);
    float ang_speed = vec3_length(body->angular_velocity);
    bool at_rest = (body->flags & PHYS_FLAG_GROUNDED) &&
                   body->ground_frames >= PHYS_GROUND_PERSIST_FRAMES &&
                   lin_speed < PHYS_SETTLE_LINEAR_THRESHOLD &&
                   ang_speed < PHYS_SETTLE_ANGULAR_THRESHOLD;

    if (at_rest)
    {
        body->velocity = vec3_zero();
        body->angular_velocity = vec3_zero();
        return;
    }

    int32_t ground_contacts = 0;
    int32_t correction_count = 0;
    Vec3 total_correction = vec3_zero();
    Vec3 world_com = vobj_world_com(obj);

    for (int32_t i = 0; i < PHYS_TERRAIN_SAMPLE_POINTS; i++)
    {
        Vec3 point = sample_points[i];

        uint8_t mat_id = volume_get_at(world->terrain, point);
        if (mat_id == 0)
            continue;

        Vec3 normal = estimate_terrain_normal(world->terrain, point, probe_dist, obj->position);
        float normal_len = vec3_length(normal);
        if (normal_len < K_EPSILON)
            continue;

        bool is_ground = normal.y > 0.85f && point.y < world_com.y;
        if (is_ground)
            ground_contacts++;

        if (!is_ground && normal.y > 0.0f)
        {
            normal.y = 0.0f;
            float hlen = sqrtf(normal.x * normal.x + normal.z * normal.z);
            if (hlen > K_EPSILON)
                normal = vec3_create(normal.x / hlen, 0.0f, normal.z / hlen);
            else
                continue;
        }

        float penetration = estimate_penetration_depth(world->terrain, point, normal, voxel_size);
        if (penetration < PHYS_SLOP)
            continue;

        Vec3 r = vec3_sub(point, world_com);
        Vec3 point_vel = get_point_velocity(body, obj, point);
        float v_n = vec3_dot(point_vel, normal);

        float eff_mass = compute_effective_mass(body, obj, r, normal);
        if (eff_mass < K_EPSILON)
            continue;

        float j_n = 0.0f;

        if (v_n < -0.01f)
        {
            float effective_restitution = body->restitution;
            if (fabsf(v_n) < PHYS_BOUNCE_DEAD_ZONE)
                effective_restitution *= fabsf(v_n) / PHYS_BOUNCE_DEAD_ZONE;

            float bias = -PHYS_BAUMGARTE_FACTOR * (1.0f / dt) * maxf(0.0f, penetration - PHYS_SLOP);
            j_n = (-(1.0f + effective_restitution) * v_n + bias) / eff_mass;
            if (j_n < 0.0f)
                j_n = 0.0f;

            Vec3 impulse_n = vec3_scale(normal, j_n);
            physics_body_apply_impulse(world, body_index, impulse_n, point);
        }
        else if (penetration > PHYS_SLOP)
        {
            float bias = PHYS_BAUMGARTE_FACTOR * (1.0f / dt) * (penetration - PHYS_SLOP);
            j_n = bias / eff_mass;
        }

        if (is_ground)
        {
            const MaterialDescriptor *mat_desc = material_get(mat_id);
            float surface_friction = mat_desc ? mat_desc->friction : body->friction;
            float mu = maxf(body->friction, surface_friction);

            Vec3 tangent = vec3_sub(point_vel, vec3_scale(normal, v_n));
            float tangent_len = vec3_length(tangent);
            if (tangent_len > K_EPSILON && j_n > K_EPSILON)
            {
                tangent = vec3_scale(tangent, 1.0f / tangent_len);
                float v_t = tangent_len;
                float j_t = -v_t / eff_mass;
                float max_friction = mu * j_n;
                j_t = clampf(j_t, -max_friction, max_friction);

                Vec3 impulse_t = vec3_scale(tangent, j_t);
                physics_body_apply_impulse(world, body_index, impulse_t, point);
            }
        }

        total_correction = vec3_add(total_correction, vec3_scale(normal, penetration));
        correction_count++;
    }

    if (ground_contacts >= 1)
    {
        body->ground_frames = PHYS_GROUND_PERSIST_FRAMES;
        body->flags |= PHYS_FLAG_GROUNDED;

        /* Use compound box corners for stability check — these follow actual
         * voxel geometry and avoid phantom contacts in empty OBB regions. */
        Vec3 compound_pts[PHYS_MAX_COMPOUND_POINTS];
        int32_t compound_count = get_collider_ground_points(obj, compound_pts, PHYS_MAX_COMPOUND_POINTS);
        Vec3 ground_centroid = vec3_zero();
        int32_t centroid_count = 0;

        for (int32_t i = 0; i < compound_count; i++)
        {
            if (compound_pts[i].y >= world_com.y)
                continue;
            Vec3 below = vec3_create(compound_pts[i].x,
                                     compound_pts[i].y - voxel_size,
                                     compound_pts[i].z);
            uint8_t mat = volume_get_at(world->terrain, below);
            if (mat == 0)
                mat = volume_get_at(world->terrain, compound_pts[i]);
            if (mat != 0)
            {
                centroid_count++;
                ground_centroid = vec3_add(ground_centroid, compound_pts[i]);
            }
        }

        if (centroid_count > 0)
        {
            ground_centroid = vec3_scale(ground_centroid, 1.0f / (float)centroid_count);
            float dx = world_com.x - ground_centroid.x;
            float dz = world_com.z - ground_centroid.z;
            float horizontal_offset = sqrtf(dx * dx + dz * dz);
            float max_he = maxf(obj->shape_half_extents.x, obj->shape_half_extents.z);
            if (horizontal_offset < max_he * PHYS_STABLE_SUPPORT_RATIO)
                body->flags |= PHYS_FLAG_STABLE;
            else
                body->flags &= ~PHYS_FLAG_STABLE;
        }
        else
        {
            body->flags &= ~PHYS_FLAG_STABLE;
        }
    }
    else if (body->ground_frames > 0)
    {
        body->ground_frames--;
        if (body->ground_frames == 0)
        {
            body->flags &= ~PHYS_FLAG_GROUNDED;
            body->flags &= ~PHYS_FLAG_STABLE;
        }
    }
    else
    {
        body->flags &= ~PHYS_FLAG_GROUNDED;
        body->flags &= ~PHYS_FLAG_STABLE;
    }

    if (correction_count > 0 && vec3_length(total_correction) > K_EPSILON)
    {
        total_correction = vec3_scale(total_correction, 1.0f / (float)correction_count);

        float corr_len = vec3_length(total_correction);
        float max_corr = voxel_size * 1.5f;
        if (corr_len > max_corr)
            total_correction = vec3_scale(total_correction, max_corr / corr_len);

        obj->position = vec3_add(obj->position, vec3_scale(total_correction, 0.8f));
    }

    if (body->flags & PHYS_FLAG_GROUNDED)
    {
        float vel_y = body->velocity.y;
        if (vel_y < 0.0f && vel_y > -1.0f)
            body->velocity.y = 0.0f;

        float lin_speed = vec3_length(body->velocity);
        float ang_speed = vec3_length(body->angular_velocity);

        if (lin_speed < PHYS_SETTLE_LINEAR_THRESHOLD)
            body->velocity = vec3_zero();
        if (ang_speed < PHYS_SETTLE_ANGULAR_THRESHOLD)
            body->angular_velocity = vec3_zero();
    }
}

static void integrate_body(PhysicsWorld *world, int32_t body_index, float dt)
{
    RigidBody *body = &world->bodies[body_index];
    if (!(body->flags & PHYS_FLAG_ACTIVE) || (body->flags & PHYS_FLAG_SLEEPING))
        return;

    if (body->flags & (PHYS_FLAG_STATIC | PHYS_FLAG_KINEMATIC))
        return;

    VoxelObject *obj = &world->objects->objects[body->vobj_index];
    if (!obj->active)
    {
        physics_world_remove_body(world, body_index);
        return;
    }

    bool grounded = (body->flags & PHYS_FLAG_GROUNDED) != 0;

    if (!grounded)
    {
        body->velocity = vec3_add(body->velocity, vec3_scale(world->gravity, dt));
    }

    float linear_damp = grounded ? PHYS_GROUND_LINEAR_DAMPING : PHYS_LINEAR_DAMPING;
    float angular_damp = grounded ? PHYS_GROUND_ANGULAR_DAMPING : PHYS_ANGULAR_DAMPING;

    body->velocity = vec3_scale(body->velocity, linear_damp);
    body->angular_velocity = vec3_scale(body->angular_velocity, angular_damp);

    body->velocity = vec3_clamp_length(body->velocity, PHYS_MAX_LINEAR_VELOCITY);
    body->angular_velocity = vec3_clamp_length(body->angular_velocity, PHYS_MAX_ANGULAR_VELOCITY);

    obj->position = vec3_add(obj->position, vec3_scale(body->velocity, dt));
    obj->orientation = quat_integrate(obj->orientation, body->angular_velocity, dt);
}

static void update_sleep_state(PhysicsWorld *world, int32_t body_index)
{
    RigidBody *body = &world->bodies[body_index];
    if (!(body->flags & PHYS_FLAG_ACTIVE))
        return;

    if (body->flags & PHYS_FLAG_STATIC)
        return;

    float linear_speed = vec3_length(body->velocity);
    float angular_speed = vec3_length(body->angular_velocity);

    bool velocity_low = linear_speed < PHYS_SLEEP_LINEAR_THRESHOLD &&
                        angular_speed < PHYS_SLEEP_ANGULAR_THRESHOLD;
    bool has_support = (body->flags & (PHYS_FLAG_STABLE | PHYS_FLAG_OBJ_CONTACT)) != 0;

    if (velocity_low && has_support)
    {
        body->sleep_frames++;
        if (body->sleep_frames >= PHYS_SLEEP_FRAMES)
        {
            body->flags |= PHYS_FLAG_SLEEPING;
            body->velocity = vec3_zero();
            body->angular_velocity = vec3_zero();
        }
    }
    else
    {
        body->sleep_frames = 0;
        body->flags &= ~PHYS_FLAG_SLEEPING;
    }
}

void physics_world_step(PhysicsWorld *world, float dt)
{
    if (!world || !world->objects)
        return;

    PROFILE_BEGIN(PROFILE_SIM_PHYSICS);

    int32_t limit = world->max_body_index + 1;

    for (int32_t i = 0; i < limit; i++)
    {
        if (world->bodies[i].flags & PHYS_FLAG_ACTIVE)
            world->bodies[i].flags &= ~PHYS_FLAG_OBJ_CONTACT;
    }

    int32_t substeps = 1;
    for (int32_t i = 0; i < limit; i++)
    {
        uint8_t flags = world->bodies[i].flags;
        if (!(flags & PHYS_FLAG_ACTIVE) || (flags & PHYS_FLAG_SLEEPING))
            continue;

        float speed = vec3_length(world->bodies[i].velocity);
        if (speed > PHYS_SUBSTEP_VELOCITY_THRESHOLD)
        {
            VoxelObject *obj = &world->objects->objects[world->bodies[i].vobj_index];
            float min_extent = minf(obj->shape_half_extents.x,
                                    minf(obj->shape_half_extents.y,
                                         obj->shape_half_extents.z)) * 2.0f;
            if (min_extent > K_EPSILON)
            {
                int32_t needed = (int32_t)ceilf(speed * dt / min_extent);
                if (needed > substeps)
                    substeps = needed;
            }
        }
    }
    if (substeps > PHYS_MAX_SUBSTEPS)
        substeps = PHYS_MAX_SUBSTEPS;

    float sub_dt = dt / (float)substeps;

    for (int32_t s = 0; s < substeps; s++)
    {
        for (int32_t i = 0; i < limit; i++)
        {
            uint8_t flags = world->bodies[i].flags;
            if (!(flags & PHYS_FLAG_ACTIVE) || (flags & PHYS_FLAG_SLEEPING))
                continue;

            integrate_body(world, i, sub_dt);
        }

        physics_process_object_collisions(world, sub_dt);

        if (world->terrain)
        {
            for (int32_t i = 0; i < limit; i++)
            {
                uint8_t flags = world->bodies[i].flags;
                if (!(flags & PHYS_FLAG_ACTIVE) || (flags & PHYS_FLAG_SLEEPING))
                    continue;

                solve_terrain_collision(world, i, sub_dt);
            }
        }
    }

    for (int32_t i = 0; i < limit; i++)
    {
        if (!(world->bodies[i].flags & PHYS_FLAG_ACTIVE))
            continue;

        update_sleep_state(world, i);
    }

    PROFILE_END(PROFILE_SIM_PHYSICS);
}

void physics_world_sync_objects(PhysicsWorld *world)
{
    if (!world || !world->objects)
        return;

    VoxelObjectWorld *obj_world = world->objects;

    for (int32_t i = 0; i < obj_world->object_count; i++)
    {
        VoxelObject *obj = &obj_world->objects[i];
        if (!obj->active)
            continue;

        int32_t body_idx = physics_world_find_body_for_object(world, i);
        if (body_idx < 0)
        {
            physics_world_add_body(world, i);
        }
    }

    int32_t limit = world->max_body_index + 1;
    for (int32_t i = 0; i < limit; i++)
    {
        RigidBody *body = &world->bodies[i];
        if (!(body->flags & PHYS_FLAG_ACTIVE))
            continue;

        if (body->vobj_index < 0 || body->vobj_index >= obj_world->object_count)
        {
            physics_world_remove_body(world, i);
            continue;
        }

        VoxelObject *obj = &obj_world->objects[body->vobj_index];
        if (!obj->active)
        {
            physics_world_remove_body(world, i);
            continue;
        }

        if (obj->voxel_revision != body->synced_revision)
        {
            body->mass = obj->total_mass > K_EPSILON ? obj->total_mass
                                                     : (float)obj->voxel_count * PHYS_VOXEL_DENSITY;
            if (body->mass < K_EPSILON)
                body->mass = K_EPSILON;
            body->inv_mass = 1.0f / body->mass;

            if (obj->inertia_diag.x > K_EPSILON && obj->inertia_diag.y > K_EPSILON &&
                obj->inertia_diag.z > K_EPSILON)
            {
                body->inertia_local = obj->inertia_diag;
                body->inv_inertia_local = vec3_create(
                    1.0f / obj->inertia_diag.x,
                    1.0f / obj->inertia_diag.y,
                    1.0f / obj->inertia_diag.z);
            }
            else
            {
                physics_body_compute_inertia(body, obj->shape_half_extents);
            }

            body->synced_revision = obj->voxel_revision;

            /* Shape changed: clear grounded/sleep so body re-evaluates contacts.
             * Without this, at_rest short-circuits terrain collision and the body
             * stays frozen after splits or destruction shift its position. */
            body->flags &= ~(PHYS_FLAG_SLEEPING | PHYS_FLAG_GROUNDED);
            body->sleep_frames = 0;
            body->ground_frames = 0;
        }
    }
}

void physics_world_wake_in_region(PhysicsWorld *world, Vec3 center, float radius)
{
    if (!world || !world->objects)
        return;

    int32_t limit = world->max_body_index + 1;

    for (int32_t i = 0; i < limit; i++)
    {
        RigidBody *body = &world->bodies[i];
        if (!(body->flags & PHYS_FLAG_ACTIVE) || !(body->flags & PHYS_FLAG_SLEEPING))
            continue;

        VoxelObject *obj = &world->objects->objects[body->vobj_index];
        if (!obj->active)
            continue;

        Vec3 delta = vec3_sub(obj->position, center);
        float dist_sq = vec3_dot(delta, delta);
        float combined = radius + obj->radius;
        if (dist_sq <= combined * combined)
        {
            physics_body_wake(world, i);
            body->flags &= ~(PHYS_FLAG_GROUNDED | PHYS_FLAG_STABLE);
            body->ground_frames = 0;
        }
    }
}
