#include "rigidbody.h"
#include "collision_object.h"
#include "content/materials.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>

STATIC_ASSERT(SAP_MAX_BODIES >= PHYS_MAX_BODIES, "every body needs a broadphase slot");
STATIC_ASSERT(PHYS_MAX_BODIES <= INT16_MAX, "body indices are stored as int16_t");

/* Share of the measured terrain penetration removed per step by moving the body */
#define PHYS_TERRAIN_POSITION_CORRECTION 0.8f
/* Approach speed below which a terrain contact counts as resting (no bounce impulse) */
#define PHYS_TERRAIN_RESTING_SPEED 0.01f
/* Quarter-voxel samples along a contact normal for the penetration depth: two voxels */
#define PHYS_PENETRATION_PROBES 8

PhysicsWorld *physics_world_create(VoxelObjectWorld *objects, VoxelVolume *terrain)
{
    PhysicsWorld *world = (PhysicsWorld *)calloc(1, sizeof(PhysicsWorld));
    if (!world)
        return NULL;

    world->broadphase = (SAPBroadphase *)calloc(1, sizeof(SAPBroadphase));
    world->hull_cache = (PhysicsHullCache *)calloc(VOBJ_MAX_OBJECTS, sizeof(PhysicsHullCache));
    if (!world->broadphase || !world->hull_cache)
    {
        physics_world_destroy(world);
        return NULL;
    }
    sap_init(world->broadphase);

    world->objects = objects;
    world->terrain = terrain;
    world->gravity = vec3_create(0.0f, PHYS_GRAVITY_Y, 0.0f);
    world->first_free = -1;
    world->max_body_index = -1;
    /* Removals before the world existed had no bodies on them */
    world->terrain_removals_seen = terrain ? terrain->removal_count : 0;

    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
        world->vobj_to_body[i] = -1;

    return world;
}

void physics_world_destroy(PhysicsWorld *world)
{
    if (!world)
        return;
    free(world->broadphase);
    free(world->hull_cache);
    free(world);
}

/* Slots up to max_body_index are in use or on the free list, so with the list empty the
 * next free slot is the one after them */
static int32_t find_free_slot(PhysicsWorld *world)
{
    if (world->first_free >= 0)
    {
        int32_t slot = world->first_free;
        world->first_free = world->bodies[slot].next_free;
        return slot;
    }
    return world->max_body_index + 1 < PHYS_MAX_BODIES ? world->max_body_index + 1 : -1;
}

static float inverse_or_zero(float x)
{
    return x > 0.0f ? 1.0f / x : 0.0f;
}

/* The body's object slot still holds the allocation the body was created for. A
 * different one (a split piece, a thrown shape, a detached island) means the slot was
 * freed and reused since: the body belongs to its previous occupant. */
static bool body_object_live(const PhysicsWorld *world, const RigidBody *body)
{
    const VoxelObject *obj = &world->objects->objects[body->vobj_index];
    return obj->active && obj->alloc_id == body->vobj_alloc_id;
}

/* voxel_object_recalc_shape gives every non-empty object a positive mass and, through each
 * voxel's own inertia, a positive inertia about every axis. A zero (an object that was
 * never recalculated) maps to an infinite one rather than to a division by zero. */
static void take_mass_properties(RigidBody *body, const VoxelObject *obj)
{
    body->inv_mass = inverse_or_zero(obj->total_mass);
    body->inv_inertia_local = vec3_create(inverse_or_zero(obj->inertia_diag.x),
                                          inverse_or_zero(obj->inertia_diag.y),
                                          inverse_or_zero(obj->inertia_diag.z));
    body->synced_revision = obj->voxel_revision;
}

static void remove_body(PhysicsWorld *world, int32_t body_index)
{
    RigidBody *body = &world->bodies[body_index];
    /* The slot's new occupant may have its own body by now */
    if (world->vobj_to_body[body->vobj_index] == body_index)
        world->vobj_to_body[body->vobj_index] = -1;
    body->flags = 0;
    body->next_free = (int16_t)world->first_free;
    world->first_free = body_index;
}

int32_t physics_world_add_body(PhysicsWorld *world, int32_t vobj_index)
{
    if (!world || vobj_index < 0 || vobj_index >= world->objects->object_count)
        return -1;

    VoxelObject *obj = &world->objects->objects[vobj_index];
    if (!obj->active)
        return -1;

    /* object_count <= VOBJ_MAX_OBJECTS, which the analyzer cannot see */
    int32_t existing = world->vobj_to_body[vobj_index]; /* NOLINT(clang-analyzer-security.ArrayBound) */
    if (existing >= 0 && (world->bodies[existing].flags & PHYS_FLAG_ACTIVE))
    {
        if (body_object_live(world, &world->bodies[existing]))
            return existing;
        remove_body(world, existing); /* the slot's previous occupant, not synced away yet */
    }

    int32_t slot = find_free_slot(world);
    if (slot < 0)
        return -1;

    RigidBody *body = &world->bodies[slot];
    memset(body, 0, sizeof(RigidBody));
    body->vobj_index = vobj_index;
    body->vobj_alloc_id = obj->alloc_id;
    body->com = physics_object_com(obj);
    take_mass_properties(body, obj);
    body->restitution = PHYS_DEFAULT_RESTITUTION;
    body->friction = PHYS_DEFAULT_FRICTION;
    body->flags = PHYS_FLAG_ACTIVE;
    body->next_free = -1;

    if (slot > world->max_body_index)
        world->max_body_index = slot;
    world->vobj_to_body[vobj_index] = (int16_t)slot;
    return slot;
}

int32_t physics_world_find_body_for_object(PhysicsWorld *world, int32_t vobj_index)
{
    if (!world || vobj_index < 0 || vobj_index >= VOBJ_MAX_OBJECTS)
        return -1;

    int16_t body_idx = world->vobj_to_body[vobj_index];
    if (body_idx >= 0 && (world->bodies[body_idx].flags & PHYS_FLAG_ACTIVE) &&
        body_object_live(world, &world->bodies[body_idx]))
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

/* Corners and face centres of the object's box (shape_half_extents about shape_center) */
static void get_obb_sample_points(const VoxelObject *obj, Vec3 points[PHYS_TERRAIN_SAMPLE_POINTS])
{
    Vec3 he = obj->shape_half_extents;
    Vec3 ax = quat_rotate_vec3(obj->orientation, vec3_create(he.x, 0.0f, 0.0f));
    Vec3 ay = quat_rotate_vec3(obj->orientation, vec3_create(0.0f, he.y, 0.0f));
    Vec3 az = quat_rotate_vec3(obj->orientation, vec3_create(0.0f, 0.0f, he.z));
    Vec3 c = voxel_object_box_center(obj);

    for (int32_t corner = 0; corner < 8; corner++)
    {
        Vec3 p = c;
        p = (corner & 1) ? vec3_add(p, ax) : vec3_sub(p, ax);
        p = (corner & 2) ? vec3_add(p, ay) : vec3_sub(p, ay);
        p = (corner & 4) ? vec3_add(p, az) : vec3_sub(p, az);
        points[corner] = p;
    }

    points[8] = vec3_add(c, ax);
    points[9] = vec3_sub(c, ax);
    points[10] = vec3_add(c, ay);
    points[11] = vec3_sub(c, ay);
    points[12] = vec3_add(c, az);
    points[13] = vec3_sub(c, az);
}

/* Bodies collide with solid materials only: they sink through water and pass through
 * ground flora */
static bool terrain_solid(const VoxelVolume *terrain, Vec3 p)
{
    return volume_is_solid_at(terrain, p);
}

/* Outward normal from the occupancy gradient, probed at growing distances; falls back
 * to the direction from the point towards the object centre */
static Vec3 estimate_terrain_normal(const VoxelVolume *terrain, Vec3 point, float probe_dist, Vec3 obj_center)
{
    static const float scales[] = {1.0f, 2.0f, 4.0f};
    for (int32_t s = 0; s < 3; s++)
    {
        float pd = probe_dist * scales[s];
        Vec3 gradient = vec3_create(
            (float)terrain_solid(terrain, vec3_create(point.x - pd, point.y, point.z)) -
                (float)terrain_solid(terrain, vec3_create(point.x + pd, point.y, point.z)),
            (float)terrain_solid(terrain, vec3_create(point.x, point.y - pd, point.z)) -
                (float)terrain_solid(terrain, vec3_create(point.x, point.y + pd, point.z)),
            (float)terrain_solid(terrain, vec3_create(point.x, point.y, point.z - pd)) -
                (float)terrain_solid(terrain, vec3_create(point.x, point.y, point.z + pd)));
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

/* Distance along the normal to the first empty terrain sample (quarter-voxel steps),
 * up to two voxels */
static float estimate_penetration_depth(const VoxelVolume *terrain, Vec3 point, Vec3 normal, float voxel_size)
{
    float step = voxel_size * 0.25f;
    for (int32_t i = 0; i < PHYS_PENETRATION_PROBES; i++)
    {
        float d = step * (float)i;
        if (!terrain_solid(terrain, vec3_add(point, vec3_scale(normal, d))))
            return d;
    }
    return step * (float)PHYS_PENETRATION_PROBES;
}

/* PHYS_FLAG_STABLE when the centre of mass sits over the centroid of the hull points
 * that touch the ground. The hull points are the object's extreme voxel corners in a
 * fixed spread of directions, so they follow the actual voxels (the empty parts of the
 * object's box do not count as support), and whatever part of an object of any size
 * and shape rests on the ground has hull points in it. */
static void update_stability(RigidBody *body, const VoxelObject *obj, const VoxelVolume *terrain, Vec3 com)
{
    Vec3 centroid = vec3_zero();
    int32_t supported = 0;

    for (int32_t i = 0; i < obj->surface_voxel_count; i++)
    {
        Vec3 p = vec3_add(obj->position, quat_rotate_vec3(obj->orientation, obj->surface_voxels[i]));
        if (p.y >= com.y)
            continue;
        Vec3 below = vec3_create(p.x, p.y - terrain->voxel_size, p.z);
        if (terrain_solid(terrain, below) || terrain_solid(terrain, p))
        {
            centroid = vec3_add(centroid, p);
            supported++;
        }
    }

    body->flags &= ~PHYS_FLAG_STABLE;
    if (supported == 0)
        return;

    centroid = vec3_scale(centroid, 1.0f / (float)supported);
    float dx = com.x - centroid.x;
    float dz = com.z - centroid.z;
    float max_he = maxf(obj->shape_half_extents.x, obj->shape_half_extents.z);
    if (sqrtf(dx * dx + dz * dz) < max_he * PHYS_STABLE_SUPPORT_RATIO)
        body->flags |= PHYS_FLAG_STABLE;
}

static void solve_terrain_collision(PhysicsWorld *world, int32_t body_index, float dt)
{
    RigidBody *body = &world->bodies[body_index];
    VoxelObject *obj = &world->objects->objects[body->vobj_index];
    const VoxelVolume *terrain = world->terrain;

    bool at_rest = (body->flags & PHYS_FLAG_GROUNDED) &&
                   body->ground_frames >= PHYS_GROUND_PERSIST_FRAMES &&
                   vec3_length(body->velocity) < PHYS_SETTLE_LINEAR_THRESHOLD &&
                   vec3_length(body->angular_velocity) < PHYS_SETTLE_ANGULAR_THRESHOLD;
    if (at_rest)
    {
        body->velocity = vec3_zero();
        body->angular_velocity = vec3_zero();
        return;
    }

    Vec3 sample_points[PHYS_TERRAIN_SAMPLE_POINTS];
    get_obb_sample_points(obj, sample_points);

    float voxel_size = terrain->voxel_size;
    int32_t ground_contacts = 0;
    int32_t correction_count = 0;
    Vec3 total_correction = vec3_zero();
    Vec3 com = physics_object_com(obj);

    for (int32_t i = 0; i < PHYS_TERRAIN_SAMPLE_POINTS; i++)
    {
        Vec3 point = sample_points[i];
        uint8_t mat_id = volume_get_at(terrain, point);
        if (!material_is_solid(mat_id))
            continue;

        Vec3 normal = estimate_terrain_normal(terrain, point, voxel_size * 0.5f, voxel_object_box_center(obj));
        if (vec3_length_sq(normal) < K_EPSILON * K_EPSILON)
            continue;

        bool is_ground = normal.y > 0.85f && point.y < com.y;
        if (is_ground)
            ground_contacts++;
        else if (normal.y > 0.0f)
        {
            /* Steep surfaces push sideways only, so bodies do not climb walls */
            float hlen = sqrtf(normal.x * normal.x + normal.z * normal.z);
            if (hlen < K_EPSILON)
                continue;
            normal = vec3_create(normal.x / hlen, 0.0f, normal.z / hlen);
        }

        float penetration = estimate_penetration_depth(terrain, point, normal, voxel_size);
        if (penetration < PHYS_SLOP)
            continue;

        Vec3 r = vec3_sub(point, com);
        float k_n = rigid_body_inv_mass_along(body, obj->orientation, r, normal);
        if (k_n <= 0.0f)
            continue;

        /* Penetration is removed by moving the body below; the impulse only handles the
         * approach velocity, so the two never add up to an extra push. */
        float v_n = vec3_dot(rigid_body_point_velocity(body, r), normal);
        float j_n;
        if (v_n < -PHYS_TERRAIN_RESTING_SPEED)
        {
            float restitution = body->restitution;
            if (-v_n < PHYS_BOUNCE_DEAD_ZONE)
                restitution *= -v_n / PHYS_BOUNCE_DEAD_ZONE;
            j_n = -(1.0f + restitution) * v_n / k_n;
            rigid_body_add_impulse(body, obj->orientation, vec3_scale(normal, j_n), r);
        }
        else
        {
            /* Resting: the penetration stands in for the support force that bounds friction */
            j_n = PHYS_BAUMGARTE_FACTOR / dt * (penetration - PHYS_SLOP) / k_n;
        }

        if (is_ground && j_n > 0.0f)
        {
            /* Friction against the sliding velocity left after the normal impulse, with the
             * effective mass along the tangent (not the normal's, which overshoots and can
             * spin a body up) */
            Vec3 point_vel = rigid_body_point_velocity(body, r);
            Vec3 tangent = vec3_sub(point_vel, vec3_scale(normal, vec3_dot(point_vel, normal)));
            float v_t = vec3_length(tangent);
            if (v_t > K_EPSILON)
            {
                tangent = vec3_scale(tangent, 1.0f / v_t);
                float k_t = rigid_body_inv_mass_along(body, obj->orientation, r, tangent);
                const MaterialDescriptor *mat_desc = material_get(mat_id);
                float mu = maxf(body->friction, mat_desc ? mat_desc->friction : body->friction);
                float j_t = minf(v_t / k_t, mu * j_n);
                rigid_body_add_impulse(body, obj->orientation, vec3_scale(tangent, -j_t), r);
            }
        }

        total_correction = vec3_add(total_correction, vec3_scale(normal, penetration));
        correction_count++;
    }

    if (ground_contacts > 0)
    {
        body->ground_frames = PHYS_GROUND_PERSIST_FRAMES;
        body->flags |= PHYS_FLAG_GROUNDED;
        update_stability(body, obj, terrain, com);
    }
    else if (body->ground_frames > 0)
    {
        body->ground_frames--;
        if (body->ground_frames == 0)
            body->flags &= ~(PHYS_FLAG_GROUNDED | PHYS_FLAG_STABLE);
    }
    else
    {
        body->flags &= ~(PHYS_FLAG_GROUNDED | PHYS_FLAG_STABLE);
    }

    if (correction_count > 0)
    {
        Vec3 correction = vec3_scale(total_correction, 1.0f / (float)correction_count);
        float corr_len = vec3_length(correction);
        float max_corr = voxel_size * 1.5f;
        if (corr_len > max_corr)
            correction = vec3_scale(correction, max_corr / corr_len);
        obj->position = vec3_add(obj->position, vec3_scale(correction, PHYS_TERRAIN_POSITION_CORRECTION));
    }

    if (body->flags & PHYS_FLAG_GROUNDED)
    {
        if (body->velocity.y < 0.0f && body->velocity.y > -1.0f)
            body->velocity.y = 0.0f;
        if (vec3_length(body->velocity) < PHYS_SETTLE_LINEAR_THRESHOLD)
            body->velocity = vec3_zero();
        if (vec3_length(body->angular_velocity) < PHYS_SETTLE_ANGULAR_THRESHOLD)
            body->angular_velocity = vec3_zero();
    }
}

static void integrate_body(PhysicsWorld *world, int32_t body_index, float dt)
{
    RigidBody *body = &world->bodies[body_index];
    VoxelObject *obj = &world->objects->objects[body->vobj_index];
    if (!body_object_live(world, body))
    {
        remove_body(world, body_index);
        return;
    }

    bool grounded = (body->flags & PHYS_FLAG_GROUNDED) != 0;
    if (!grounded)
        body->velocity = vec3_add(body->velocity, vec3_scale(world->gravity, dt));

    float frames = dt * PHYS_DAMPING_REFERENCE_HZ;
    float linear_damp = powf(grounded ? PHYS_GROUND_LINEAR_DAMPING : PHYS_LINEAR_DAMPING, frames);
    float angular_damp = powf(grounded ? PHYS_GROUND_ANGULAR_DAMPING : PHYS_ANGULAR_DAMPING, frames);
    body->velocity = vec3_clamp_length(vec3_scale(body->velocity, linear_damp), PHYS_MAX_LINEAR_VELOCITY);
    body->angular_velocity = vec3_clamp_length(vec3_scale(body->angular_velocity, angular_damp),
                                               PHYS_MAX_ANGULAR_VELOCITY);

    /* Move the centre of mass and rotate about it; obj->position is the grid centre */
    Vec3 com = vec3_add(physics_object_com(obj), vec3_scale(body->velocity, dt));
    obj->orientation = quat_integrate(obj->orientation, body->angular_velocity, dt);
    obj->position = vec3_sub(com, quat_rotate_vec3(obj->orientation, obj->local_com));
}

/* Only bodies resting stably on terrain sleep; terrain removed under them wakes them
 * (wake_bodies_over_removed_terrain) */
static void update_sleep_state(RigidBody *body)
{
    bool velocity_low = vec3_length(body->velocity) < PHYS_SLEEP_LINEAR_THRESHOLD &&
                        vec3_length(body->angular_velocity) < PHYS_SLEEP_ANGULAR_THRESHOLD;

    if (velocity_low && (body->flags & PHYS_FLAG_STABLE))
    {
        if (body->sleep_frames < PHYS_SLEEP_FRAMES)
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

/* World AABB of the object's rotated voxel box: extent_i = sum_j |R_ij| * he_j about the
 * box centre. The box holds every voxel corner, so the hull, too. */
static void object_world_aabb(const VoxelObject *obj, Vec3 *out_min, Vec3 *out_max)
{
    Vec3 he = obj->shape_half_extents;
    float m[9];
    quat_to_mat3(obj->orientation, m);
    Vec3 ext = vec3_create(fabsf(m[0]) * he.x + fabsf(m[1]) * he.y + fabsf(m[2]) * he.z,
                           fabsf(m[3]) * he.x + fabsf(m[4]) * he.y + fabsf(m[5]) * he.z,
                           fabsf(m[6]) * he.x + fabsf(m[7]) * he.y + fabsf(m[8]) * he.z);
    Vec3 center = voxel_object_box_center(obj);
    *out_min = vec3_sub(center, ext);
    *out_max = vec3_add(center, ext);
}

static void update_broadphase(PhysicsWorld *world)
{
    for (int32_t i = 0; i <= world->max_body_index; i++)
    {
        const RigidBody *body = &world->bodies[i];
        if (!(body->flags & PHYS_FLAG_ACTIVE) || !body_object_live(world, body))
        {
            sap_remove_body(world->broadphase, i);
            continue;
        }

        Vec3 aabb_min, aabb_max;
        object_world_aabb(&world->objects->objects[body->vobj_index], &aabb_min, &aabb_max);
        sap_update_body(world->broadphase, i, aabb_min, aabb_max);
    }
}

/* A body whose support may have changed: awake bodies at rest skip their ground check
 * too, so both kinds are made to re-check it */
static void wake_to_recheck_ground(PhysicsWorld *world, int32_t body_index)
{
    RigidBody *body = &world->bodies[body_index];
    physics_body_wake(world, body_index);
    body->flags &= ~(PHYS_FLAG_GROUNDED | PHYS_FLAG_STABLE);
    body->ground_frames = 0;
}

/* Terrain emptied since the last step (digs, detached islands taken out: see the
 * volume's removal log) may have held bodies up. Wake every body whose box overlaps a
 * removed box grown by a voxel, which takes in the bodies resting on its top. */
static void wake_bodies_over_removed_terrain(PhysicsWorld *world)
{
    const VoxelVolume *terrain = world->terrain;
    uint32_t pending = terrain->removal_count - world->terrain_removals_seen;
    if (pending == 0)
        return;
    world->terrain_removals_seen = terrain->removal_count;

    /* Fell behind the log: every body may stand on lost terrain */
    bool lost_entries = pending > VOLUME_REMOVAL_LOG_SIZE;

    Vec3 removed_min[VOLUME_REMOVAL_LOG_SIZE];
    Vec3 removed_max[VOLUME_REMOVAL_LOG_SIZE];
    int32_t removed_count = 0;
    for (uint32_t n = terrain->removal_count - pending; !lost_entries && n != terrain->removal_count; n++)
    {
        const VoxelBox *box = &terrain->removal_log[n % VOLUME_REMOVAL_LOG_SIZE];
        float vs = terrain->voxel_size;
        removed_min[removed_count] = vec3_create(terrain->bounds.min_x + (float)(box->min_x - 1) * vs,
                                                 terrain->bounds.min_y + (float)(box->min_y - 1) * vs,
                                                 terrain->bounds.min_z + (float)(box->min_z - 1) * vs);
        removed_max[removed_count] = vec3_create(terrain->bounds.min_x + (float)(box->max_x + 2) * vs,
                                                 terrain->bounds.min_y + (float)(box->max_y + 2) * vs,
                                                 terrain->bounds.min_z + (float)(box->max_z + 2) * vs);
        removed_count++;
    }

    for (int32_t i = 0; i <= world->max_body_index; i++)
    {
        const RigidBody *body = &world->bodies[i];
        if (!(body->flags & PHYS_FLAG_ACTIVE) || (body->flags & PHYS_FLAG_STATIC) ||
            !body_object_live(world, body))
            continue;

        bool affected = lost_entries;
        if (!affected)
        {
            Vec3 aabb_min, aabb_max;
            object_world_aabb(&world->objects->objects[body->vobj_index], &aabb_min, &aabb_max);
            for (int32_t r = 0; r < removed_count && !affected; r++)
            {
                affected = aabb_min.x <= removed_max[r].x && aabb_max.x >= removed_min[r].x &&
                           aabb_min.y <= removed_max[r].y && aabb_max.y >= removed_min[r].y &&
                           aabb_min.z <= removed_max[r].z && aabb_max.z >= removed_min[r].z;
            }
        }
        if (affected)
            wake_to_recheck_ground(world, i);
    }
}

void physics_world_step(PhysicsWorld *world, float dt)
{
    if (!world || dt <= 0.0f)
        return;

    PROFILE_BEGIN(PROFILE_SIM_PHYSICS);

    if (world->terrain)
        wake_bodies_over_removed_terrain(world);

    int32_t limit = world->max_body_index + 1;

    /* Substep so that no fast body moves more than its smallest extent per substep */
    int32_t substeps = 1;
    for (int32_t i = 0; i < limit; i++)
    {
        const RigidBody *body = &world->bodies[i];
        if (!rigid_body_is_dynamic(body))
            continue;

        float speed = vec3_length(body->velocity);
        if (speed > PHYS_SUBSTEP_VELOCITY_THRESHOLD)
        {
            Vec3 he = world->objects->objects[body->vobj_index].shape_half_extents;
            float min_extent = minf(he.x, minf(he.y, he.z)) * 2.0f;
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
            if (rigid_body_is_dynamic(&world->bodies[i]))
                integrate_body(world, i, sub_dt);
        }

        update_broadphase(world);
        physics_process_object_collisions(world);

        if (world->terrain)
        {
            for (int32_t i = 0; i < limit; i++)
            {
                if (rigid_body_is_dynamic(&world->bodies[i]))
                    solve_terrain_collision(world, i, sub_dt);
            }
        }
    }

    for (int32_t i = 0; i < limit; i++)
    {
        RigidBody *body = &world->bodies[i];
        if (!(body->flags & PHYS_FLAG_ACTIVE))
            continue;
        if (!(body->flags & PHYS_FLAG_STATIC))
            update_sleep_state(body);
        body->com = physics_object_com(&world->objects->objects[body->vobj_index]);
    }

    PROFILE_END(PROFILE_SIM_PHYSICS);
}

/* Body for an object that has none yet. A piece split off another object takes over the
 * parent's rigid motion at its own centre of mass. The parent may be a piece from the
 * same tick itself, so it gets its body (and its motion) first. */
static int32_t add_body_for_new_object(PhysicsWorld *world, int32_t vobj_index)
{
    VoxelObject *obj = &world->objects->objects[vobj_index];
    int32_t parent_obj = obj->split_source - 1;
    obj->split_source = 0;

    int32_t body_index = physics_world_add_body(world, vobj_index);
    if (body_index < 0 || parent_obj < 0 || parent_obj >= world->objects->object_count)
        return body_index;

    int32_t parent_body = physics_world_find_body_for_object(world, parent_obj);
    /* parent_obj < object_count <= VOBJ_MAX_OBJECTS, which the analyzer cannot see */
    if (parent_body < 0 && world->objects->objects[parent_obj].active) /* NOLINT(clang-analyzer-security.ArrayBound) */
        parent_body = add_body_for_new_object(world, parent_obj);
    if (parent_body < 0)
        return body_index;

    const RigidBody *parent = &world->bodies[parent_body];
    RigidBody *body = &world->bodies[body_index];
    body->velocity = rigid_body_point_velocity(parent, vec3_sub(body->com, parent->com));
    body->angular_velocity = parent->angular_velocity;
    return body_index;
}

void physics_world_sync_objects(PhysicsWorld *world)
{
    if (!world)
        return;

    VoxelObjectWorld *obj_world = world->objects;

    for (int32_t i = 0; i <= world->max_body_index; i++)
    {
        RigidBody *body = &world->bodies[i];
        if (!(body->flags & PHYS_FLAG_ACTIVE))
            continue;

        if (!body_object_live(world, body))
        {
            remove_body(world, i);
            continue;
        }

        VoxelObject *obj = &obj_world->objects[body->vobj_index];

        if (obj->voxel_revision != body->synced_revision)
        {
            /* The velocity is that of the old centre of mass; carry the rigid motion
             * over to where the centre of mass is now */
            Vec3 com = physics_object_com(obj);
            body->velocity = rigid_body_point_velocity(body, vec3_sub(com, body->com));
            body->com = com;
            take_mass_properties(body, obj);

            /* Shape changed: re-evaluate contacts. Without this, at_rest short-circuits
             * terrain collision and the body stays frozen after splits or destruction. */
            body->flags &= ~(PHYS_FLAG_SLEEPING | PHYS_FLAG_GROUNDED | PHYS_FLAG_STABLE);
            body->sleep_frames = 0;
            body->ground_frames = 0;
        }
    }

    for (int32_t i = 0; i < obj_world->object_count; i++)
    {
        if (obj_world->objects[i].active && physics_world_find_body_for_object(world, i) < 0)
            add_body_for_new_object(world, i);
    }
}
