#include "collision_object.h"
#include "convex_hull.h"
#include "gjk.h"
#include "engine/core/spatial_hash.h"
#include <string.h>

#define COLLISION_SAMPLE_POINTS 24

typedef struct
{
    ConvexHull hull;
    uint32_t revision;
    bool valid;
} CachedHull;

static CachedHull g_hull_cache[VOBJ_MAX_OBJECTS];

static void ensure_hull_valid(VoxelObject *obj, int32_t obj_index)
{
    CachedHull *cache = &g_hull_cache[obj_index];
    if (cache->valid && cache->revision == obj->voxel_revision)
        return;

    convex_hull_build(obj->surface_voxels, obj->surface_voxel_count, &cache->hull);
    cache->revision = obj->voxel_revision;
    cache->valid = true;
}

static bool test_sphere_sphere_coarse(VoxelObject *a, VoxelObject *b)
{
    Vec3 delta = vec3_sub(b->position, a->position);
    float dist_sq = vec3_length_sq(delta);
    float combined_radius = a->radius + b->radius;
    return dist_sq <= combined_radius * combined_radius;
}

static void get_obb_axes(VoxelObject *obj, Vec3 axes[3])
{
    float mat3[9];
    quat_to_mat3(obj->orientation, mat3);
    axes[0] = vec3_create(mat3[0], mat3[3], mat3[6]);
    axes[1] = vec3_create(mat3[1], mat3[4], mat3[7]);
    axes[2] = vec3_create(mat3[2], mat3[5], mat3[8]);
}

static float project_obb_onto_axis(VoxelObject *obj, Vec3 axes[3], Vec3 axis)
{
    Vec3 he = obj->shape_half_extents;
    return he.x * fabsf(vec3_dot(axes[0], axis)) +
           he.y * fabsf(vec3_dot(axes[1], axis)) +
           he.z * fabsf(vec3_dot(axes[2], axis));
}

static bool test_sat_axis(VoxelObject *a, VoxelObject *b,
                          Vec3 axes_a[3], Vec3 axes_b[3],
                          Vec3 axis, float *min_overlap, Vec3 *min_axis)
{
    float axis_len = vec3_length(axis);
    if (axis_len < K_EPSILON)
        return true;

    axis = vec3_scale(axis, 1.0f / axis_len);

    float proj_a = project_obb_onto_axis(a, axes_a, axis);
    float proj_b = project_obb_onto_axis(b, axes_b, axis);

    Vec3 center_diff = vec3_sub(b->position, a->position);
    float center_dist = fabsf(vec3_dot(center_diff, axis));

    float overlap = proj_a + proj_b - center_dist;

    if (overlap < 0.0f)
        return false;

    if (overlap < *min_overlap)
    {
        *min_overlap = overlap;
        if (vec3_dot(center_diff, axis) < 0.0f)
            *min_axis = vec3_neg(axis);
        else
            *min_axis = axis;
    }

    return true;
}

static bool test_obb_overlap(VoxelObject *obj_a, VoxelObject *obj_b,
                              float *out_overlap, Vec3 *out_axis)
{
    Vec3 axes_a[3], axes_b[3];
    get_obb_axes(obj_a, axes_a);
    get_obb_axes(obj_b, axes_b);

    float min_overlap = 1e10f;
    Vec3 min_axis = vec3_zero();

    for (int i = 0; i < 3; i++)
    {
        if (!test_sat_axis(obj_a, obj_b, axes_a, axes_b, axes_a[i], &min_overlap, &min_axis))
            return false;
    }

    for (int i = 0; i < 3; i++)
    {
        if (!test_sat_axis(obj_a, obj_b, axes_a, axes_b, axes_b[i], &min_overlap, &min_axis))
            return false;
    }

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            Vec3 cross_axis = vec3_cross(axes_a[i], axes_b[j]);
            if (!test_sat_axis(obj_a, obj_b, axes_a, axes_b, cross_axis, &min_overlap, &min_axis))
                return false;
        }
    }

    *out_overlap = min_overlap;
    *out_axis = min_axis;
    return min_overlap > K_EPSILON;
}

static bool world_to_voxel(VoxelObject *obj, Vec3 world_point,
                           int32_t *out_vx, int32_t *out_vy, int32_t *out_vz)
{
    Vec3 relative = vec3_sub(world_point, obj->position);
    Quat inv_orient = quat_conjugate(obj->orientation);
    Vec3 local = quat_rotate_vec3(inv_orient, relative);

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float inv_voxel = 1.0f / obj->voxel_size;

    int32_t vx = (int32_t)floorf(local.x * inv_voxel + half_grid);
    int32_t vy = (int32_t)floorf(local.y * inv_voxel + half_grid);
    int32_t vz = (int32_t)floorf(local.z * inv_voxel + half_grid);

    *out_vx = vx;
    *out_vy = vy;
    *out_vz = vz;

    return (vx >= 0 && vx < VOBJ_GRID_SIZE &&
            vy >= 0 && vy < VOBJ_GRID_SIZE &&
            vz >= 0 && vz < VOBJ_GRID_SIZE);
}

static bool is_voxel_occupied(VoxelObject *obj, int32_t vx, int32_t vy, int32_t vz)
{
    if (vx < 0 || vx >= VOBJ_GRID_SIZE ||
        vy < 0 || vy >= VOBJ_GRID_SIZE ||
        vz < 0 || vz >= VOBJ_GRID_SIZE)
        return false;
    return obj->voxels[vobj_index(vx, vy, vz)].material != 0;
}

static Vec3 estimate_surface_normal(VoxelObject *obj, int32_t vx, int32_t vy, int32_t vz)
{
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;

    if (!is_voxel_occupied(obj, vx - 1, vy, vz)) nx += 1.0f;
    if (!is_voxel_occupied(obj, vx + 1, vy, vz)) nx -= 1.0f;
    if (!is_voxel_occupied(obj, vx, vy - 1, vz)) ny += 1.0f;
    if (!is_voxel_occupied(obj, vx, vy + 1, vz)) ny -= 1.0f;
    if (!is_voxel_occupied(obj, vx, vy, vz - 1)) nz += 1.0f;
    if (!is_voxel_occupied(obj, vx, vy, vz + 1)) nz -= 1.0f;

    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len > K_EPSILON)
        return vec3_create(nx / len, ny / len, nz / len);
    return vec3_create(0.0f, 1.0f, 0.0f);
}

static bool refine_collision_with_voxels(VoxelObject *obj_a, VoxelObject *obj_b,
                                          Vec3 sat_axis, float sat_overlap,
                                          Vec3 *out_contact, Vec3 *out_normal, float *out_penetration)
{
    Vec3 contact_sum = vec3_zero();
    Vec3 normal_sum = vec3_zero();
    float max_pen = 0.0f;
    int32_t contact_count = 0;

    Vec3 center_a = obj_a->position;
    Vec3 center_b = obj_b->position;
    Vec3 midpoint = vec3_scale(vec3_add(center_a, center_b), 0.5f);

    float sample_radius = sat_overlap + obj_a->voxel_size * 2.0f;

    static const float offsets[COLLISION_SAMPLE_POINTS][3] = {
        {0, 0, 0},
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {-1, -1, 0},
        {1, 0, 1}, {1, 0, -1}, {-1, 0, 1}, {-1, 0, -1},
        {0, 1, 1}, {0, 1, -1}, {0, -1, 1}, {0, -1, -1},
        {1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {-1, 1, 1}
    };

    float step = sample_radius / 2.0f;

    for (int32_t i = 0; i < COLLISION_SAMPLE_POINTS; i++)
    {
        Vec3 sample_world = vec3_add(midpoint, vec3_create(
            offsets[i][0] * step,
            offsets[i][1] * step,
            offsets[i][2] * step));

        int32_t ax, ay, az;
        if (!world_to_voxel(obj_a, sample_world, &ax, &ay, &az))
            continue;
        if (!is_voxel_occupied(obj_a, ax, ay, az))
            continue;

        int32_t bx, by, bz;
        if (!world_to_voxel(obj_b, sample_world, &bx, &by, &bz))
            continue;
        if (!is_voxel_occupied(obj_b, bx, by, bz))
            continue;

        Vec3 local_normal_a = estimate_surface_normal(obj_a, ax, ay, az);
        Vec3 world_normal_a = quat_rotate_vec3(obj_a->orientation, local_normal_a);
        Vec3 local_normal_b = estimate_surface_normal(obj_b, bx, by, bz);
        Vec3 world_normal_b = quat_rotate_vec3(obj_b->orientation, local_normal_b);

        Vec3 combined = vec3_sub(world_normal_a, world_normal_b);
        float combined_len = vec3_length(combined);
        if (combined_len > K_EPSILON)
            combined = vec3_scale(combined, 1.0f / combined_len);
        else
            combined = sat_axis;

        contact_sum = vec3_add(contact_sum, sample_world);
        normal_sum = vec3_add(normal_sum, combined);
        contact_count++;

        float pen = obj_a->voxel_size * 0.5f + obj_b->voxel_size * 0.5f;
        if (pen > max_pen)
            max_pen = pen;
    }

    if (contact_count == 0)
        return false;

    float inv_count = 1.0f / (float)contact_count;
    *out_contact = vec3_scale(contact_sum, inv_count);

    float normal_len = vec3_length(normal_sum);
    if (normal_len > K_EPSILON)
    {
        Vec3 computed_normal = vec3_scale(normal_sum, 1.0f / normal_len);
        if (vec3_dot(computed_normal, sat_axis) < 0.0f)
            computed_normal = vec3_neg(computed_normal);
        *out_normal = computed_normal;
    }
    else
    {
        *out_normal = sat_axis;
    }

    *out_penetration = max_pen * (1.0f + 0.5f * (float)contact_count / (float)COLLISION_SAMPLE_POINTS);

    return true;
}

static bool detect_obb_collision(VoxelObject *obj_a, VoxelObject *obj_b,
                                  Vec3 *out_contact, Vec3 *out_normal, float *out_penetration)
{
    float sat_overlap;
    Vec3 sat_axis;

    if (!test_obb_overlap(obj_a, obj_b, &sat_overlap, &sat_axis))
        return false;

    if (refine_collision_with_voxels(obj_a, obj_b, sat_axis, sat_overlap,
                                      out_contact, out_normal, out_penetration))
    {
        return true;
    }

    *out_contact = vec3_scale(vec3_add(obj_a->position, obj_b->position), 0.5f);
    *out_normal = sat_axis;
    *out_penetration = sat_overlap;
    return true;
}

static bool detect_hull_collision(VoxelObject *obj_a, int32_t idx_a,
                                   VoxelObject *obj_b, int32_t idx_b,
                                   Vec3 *out_contact, Vec3 *out_normal,
                                   float *out_penetration)
{
    if (obj_a->surface_voxel_count < 4 || obj_b->surface_voxel_count < 4)
    {
        return detect_obb_collision(obj_a, obj_b, out_contact, out_normal, out_penetration);
    }

    ensure_hull_valid(obj_a, idx_a);
    ensure_hull_valid(obj_b, idx_b);

    CachedHull *cache_a = &g_hull_cache[idx_a];
    CachedHull *cache_b = &g_hull_cache[idx_b];

    if (cache_a->hull.vertex_count < 4 || cache_b->hull.vertex_count < 4)
    {
        return detect_obb_collision(obj_a, obj_b, out_contact, out_normal, out_penetration);
    }

    GJKSimplex simplex;
    if (!gjk_intersect(&cache_a->hull, obj_a->position, obj_a->orientation,
                       &cache_b->hull, obj_b->position, obj_b->orientation,
                       &simplex))
    {
        return false;
    }

    EPAResult epa;
    if (!epa_penetration(&cache_a->hull, obj_a->position, obj_a->orientation,
                         &cache_b->hull, obj_b->position, obj_b->orientation,
                         &simplex, &epa))
    {
        *out_contact = vec3_scale(vec3_add(obj_a->position, obj_b->position), 0.5f);
        *out_normal = vec3_normalize(vec3_sub(obj_b->position, obj_a->position));
        *out_penetration = 0.01f;
        return true;
    }

    *out_contact = vec3_scale(vec3_add(epa.contact_a, epa.contact_b), 0.5f);
    *out_normal = epa.normal;
    *out_penetration = epa.depth;
    return true;
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

            if (!test_sphere_sphere_coarse(obj_a, obj_b))
                continue;

            Vec3 contact, normal;
            float penetration;

            if (detect_hull_collision(obj_a, body_a->vobj_index, obj_b, body_b->vobj_index,
                                       &contact, &normal, &penetration))
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
    Vec3 n = vec3_neg(pair->contact_normal);

    Vec3 vel_a = get_point_vel(body_a, obj_a, pair->contact_point);
    Vec3 vel_b = get_point_vel(body_b, obj_b, pair->contact_point);
    Vec3 rel_vel = vec3_sub(vel_a, vel_b);

    float v_n = vec3_dot(rel_vel, n);

    if (v_n >= 0.0f)
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
