#include "collision_object.h"
#include "gjk.h"

/* Share of the penetration beyond PHYS_SLOP removed per step by moving the bodies apart */
#define PHYS_OBJ_POSITION_CORRECTION 0.8f
/* Approach speed below which contacts bounce proportionally less */
#define PHYS_OBJ_BOUNCE_DEAD_ZONE 0.5f
/* Contact normals with |n.y| above this have one body resting on the other */
#define PHYS_OBJ_SUPPORT_NORMAL_Y 0.5f

/* The object's convex hull, rebuilt when its voxels changed since the cached one */
static const ConvexHull *object_hull(PhysicsWorld *world, const VoxelObject *obj, int32_t obj_index)
{
    PhysicsHullCache *cache = &world->hull_cache[obj_index];
    if (cache->revision != obj->voxel_revision)
    {
        convex_hull_build(obj->surface_voxels, obj->surface_voxel_count, &cache->hull);
        cache->hull.margin = PHYS_HULL_MARGIN;
        cache->revision = obj->voxel_revision;
    }
    return &cache->hull;
}

static bool detect_contact(PhysicsWorld *world, const RigidBody *body_a, const RigidBody *body_b,
                           ObjectCollisionPair *out)
{
    const VoxelObject *obj_a = &world->objects->objects[body_a->vobj_index];
    const VoxelObject *obj_b = &world->objects->objects[body_b->vobj_index];

    float reach = obj_a->radius + obj_b->radius;
    if (vec3_length_sq(vec3_sub(obj_b->position, obj_a->position)) > reach * reach)
        return false;

    const ConvexHull *hull_a = object_hull(world, obj_a, body_a->vobj_index);
    const ConvexHull *hull_b = object_hull(world, obj_b, body_b->vobj_index);

    GJKSimplex simplex;
    EPAResult epa;
    if (!gjk_intersect(hull_a, obj_a->position, obj_a->orientation,
                       hull_b, obj_b->position, obj_b->orientation, &simplex) ||
        !epa_penetration(hull_a, obj_a->position, obj_a->orientation,
                         hull_b, obj_b->position, obj_b->orientation, &simplex, &epa))
        return false;

    /* The margins only round the hulls off for GJK/EPA: hulls closer than the margins
     * but not overlapping are not in contact */
    float depth = epa.depth - hull_a->margin - hull_b->margin;
    if (depth < K_EPSILON)
        return false;

    out->contact_point = vec3_scale(vec3_add(epa.contact_a, epa.contact_b), 0.5f);
    out->contact_normal = epa.normal;
    out->penetration = depth;
    return true;
}

int32_t physics_detect_object_pairs(PhysicsWorld *world, ObjectCollisionPair *pairs, int32_t max_pairs)
{
    SAPPair candidates[SAP_MAX_PAIRS];
    int32_t candidate_count = sap_query_pairs(world->broadphase, candidates, SAP_MAX_PAIRS);

    int32_t pair_count = 0;
    for (int32_t p = 0; p < candidate_count && pair_count < max_pairs; p++)
    {
        const RigidBody *body_a = &world->bodies[candidates[p].body_a];
        const RigidBody *body_b = &world->bodies[candidates[p].body_b];
        if (!(body_a->flags & PHYS_FLAG_ACTIVE) || !(body_b->flags & PHYS_FLAG_ACTIVE))
            continue;
        if (!rigid_body_is_dynamic(body_a) && !rigid_body_is_dynamic(body_b))
            continue;

        ObjectCollisionPair *pair = &pairs[pair_count];
        if (detect_contact(world, body_a, body_b, pair))
        {
            pair->body_a = candidates[p].body_a;
            pair->body_b = candidates[p].body_b;
            pair_count++;
        }
    }
    return pair_count;
}

/* The body rests on terrain right below `point`, where the other body presses it down,
 * so the terrain carries that push. Probed half a voxel under the body's lowest point:
 * a body pressed beyond a ledge, or one only just off the ground, is not held. */
static bool terrain_holds_body(const PhysicsWorld *world, const RigidBody *body, const VoxelObject *obj, Vec3 point)
{
    if (!(body->flags & PHYS_FLAG_GROUNDED) || !world->terrain)
        return false;

    float m[9];
    quat_to_mat3(obj->orientation, m);
    Vec3 he = obj->shape_half_extents;
    float bottom = voxel_object_box_center(obj).y - (fabsf(m[3]) * he.x + fabsf(m[4]) * he.y + fabsf(m[5]) * he.z);
    Vec3 below = vec3_create(point.x, bottom - world->terrain->voxel_size * 0.5f, point.z);
    return volume_is_solid_at(world->terrain, below);
}

/* Applies +impulse to A and -impulse to B, each only if it is dynamic */
static void apply_pair_impulse(RigidBody *body_a, const VoxelObject *obj_a, Vec3 r_a, bool dyn_a,
                               RigidBody *body_b, const VoxelObject *obj_b, Vec3 r_b, bool dyn_b, Vec3 impulse)
{
    if (dyn_a)
        rigid_body_add_impulse(body_a, obj_a->orientation, impulse, r_a);
    if (dyn_b)
        rigid_body_add_impulse(body_b, obj_b->orientation, vec3_neg(impulse), r_b);
}

static void resolve_contact(PhysicsWorld *world, const ObjectCollisionPair *pair)
{
    RigidBody *body_a = &world->bodies[pair->body_a];
    RigidBody *body_b = &world->bodies[pair->body_b];
    VoxelObject *obj_a = &world->objects->objects[body_a->vobj_index];
    VoxelObject *obj_b = &world->objects->objects[body_b->vobj_index];

    /* n points from B to A: a positive impulse along n pushes A away from B */
    Vec3 n = vec3_neg(pair->contact_normal);
    Vec3 r_a = vec3_sub(pair->contact_point, physics_object_com(obj_a));
    Vec3 r_b = vec3_sub(pair->contact_point, physics_object_com(obj_b));
    Vec3 rel_vel = vec3_sub(rigid_body_point_velocity(body_a, r_a), rigid_body_point_velocity(body_b, r_b));
    float v_n = vec3_dot(rel_vel, n);

    /* A grounded body pressed down by the other one is held up by terrain, which this
     * contact does not see. Sharing the response by mass would push it into the ground
     * (and once at rest the terrain solver no longer pushes it back out), so it takes
     * none, as the ground would. If that leaves nothing to move, share it after all. */
    bool held_a = n.y < -PHYS_OBJ_SUPPORT_NORMAL_Y && terrain_holds_body(world, body_a, obj_a, pair->contact_point);
    bool held_b = n.y > PHYS_OBJ_SUPPORT_NORMAL_Y && terrain_holds_body(world, body_b, obj_b, pair->contact_point);

    /* An impact wakes a sleeping body, unless it only presses the body into the ground:
     * held, it would not move anyway. A sleeping body holds still like a static one, so
     * neighbours resting against it or on top of it leave it asleep. (Those on top never
     * sleep themselves: sleep needs terrain support, PHYS_FLAG_STABLE.) */
    if (v_n < -PHYS_SLEEP_LINEAR_THRESHOLD)
    {
        if ((body_a->flags & PHYS_FLAG_SLEEPING) && !held_a)
            physics_body_wake(world, pair->body_a);
        if ((body_b->flags & PHYS_FLAG_SLEEPING) && !held_b)
            physics_body_wake(world, pair->body_b);
    }

    bool dyn_a = rigid_body_is_dynamic(body_a);
    bool dyn_b = rigid_body_is_dynamic(body_b);
    if ((dyn_a && !held_a) || (dyn_b && !held_b))
    {
        dyn_a = dyn_a && !held_a;
        dyn_b = dyn_b && !held_b;
    }
    float inv_mass_a = dyn_a ? body_a->inv_mass : 0.0f;
    float inv_mass_b = dyn_b ? body_b->inv_mass : 0.0f;
    float total_inv_mass = inv_mass_a + inv_mass_b;
    if (total_inv_mass <= 0.0f)
        return;

    if (pair->penetration > PHYS_SLOP)
    {
        float correction = (pair->penetration - PHYS_SLOP) * PHYS_OBJ_POSITION_CORRECTION / total_inv_mass;
        obj_a->position = vec3_add(obj_a->position, vec3_scale(n, correction * inv_mass_a));
        obj_b->position = vec3_sub(obj_b->position, vec3_scale(n, correction * inv_mass_b));
    }

    if (v_n >= 0.0f)
        return; /* separating */

    float k_n = (dyn_a ? rigid_body_inv_mass_along(body_a, obj_a->orientation, r_a, n) : 0.0f) +
                (dyn_b ? rigid_body_inv_mass_along(body_b, obj_b->orientation, r_b, n) : 0.0f);
    float restitution = minf(body_a->restitution, body_b->restitution);
    if (-v_n < PHYS_OBJ_BOUNCE_DEAD_ZONE)
        restitution *= -v_n / PHYS_OBJ_BOUNCE_DEAD_ZONE;
    float j_n = -(1.0f + restitution) * v_n / k_n;
    apply_pair_impulse(body_a, obj_a, r_a, dyn_a, body_b, obj_b, r_b, dyn_b, vec3_scale(n, j_n));

    /* Friction against the sliding velocity left after the normal impulse, with the
     * effective mass along the tangent */
    rel_vel = vec3_sub(rigid_body_point_velocity(body_a, r_a), rigid_body_point_velocity(body_b, r_b));
    Vec3 tangent = vec3_sub(rel_vel, vec3_scale(n, vec3_dot(rel_vel, n)));
    float v_t = vec3_length(tangent);
    if (v_t < K_EPSILON)
        return;
    tangent = vec3_scale(tangent, 1.0f / v_t);

    float k_t = (dyn_a ? rigid_body_inv_mass_along(body_a, obj_a->orientation, r_a, tangent) : 0.0f) +
                (dyn_b ? rigid_body_inv_mass_along(body_b, obj_b->orientation, r_b, tangent) : 0.0f);
    float mu = (body_a->friction + body_b->friction) * 0.5f;
    float j_t = minf(v_t / k_t, mu * j_n);
    apply_pair_impulse(body_a, obj_a, r_a, dyn_a, body_b, obj_b, r_b, dyn_b, vec3_scale(tangent, -j_t));
}

void physics_process_object_collisions(PhysicsWorld *world)
{
    ObjectCollisionPair pairs[PHYS_OBJ_COLLISION_BUDGET];
    int32_t pair_count = physics_detect_object_pairs(world, pairs, PHYS_OBJ_COLLISION_BUDGET);

    for (int32_t i = 0; i < pair_count; i++)
        resolve_contact(world, &pairs[i]);
}
