#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/voxel_object.h"
#include "engine/voxel/bvh.h"
#include "engine/voxel/connectivity.h"
#include "engine/sim/detach.h"
#include "engine/voxel/volume.h"
#include "engine/physics/rigidbody.h"
#include "engine/physics/collision_object.h"
#include "engine/physics/particles.h"
#include "engine/physics/convex_hull.h"
#include "engine/physics/gjk.h"
#include "engine/platform/platform.h"
#include "content/materials.h"
#include "test_common.h"
#include "test_fixtures.h"
#include <string.h>
#include <math.h>

#ifdef PATCH_SANITIZERS_ENABLED
#define PERF_TIME_SCALE 3.0f
#else
#define PERF_TIME_SCALE 1.0f
#endif

/* Rotation of angle radians about the unit axis */
static Quat axis_angle(Vec3 axis, float angle)
{
    float s = sinf(angle * 0.5f);
    return quat_create(axis.x * s, axis.y * s, axis.z * s, cosf(angle * 0.5f));
}

/* Uniform in [-0.5, 0.5) */
static float signed_half(RngState *rng)
{
    return rng_float(rng) - 0.5f;
}

TEST(mat3_transpose_unit)
{
    float m[9], t[9];
    m[0] = 1.0f;
    m[1] = 2.0f;
    m[2] = 3.0f;
    m[3] = 4.0f;
    m[4] = 5.0f;
    m[5] = 6.0f;
    m[6] = 7.0f;
    m[7] = 8.0f;
    m[8] = 9.0f;

    mat3_transpose(m, t);

    ASSERT_NEAR(t[0], 1.0f, K_EPSILON);
    ASSERT_NEAR(t[1], 4.0f, K_EPSILON);
    ASSERT_NEAR(t[2], 7.0f, K_EPSILON);
    ASSERT_NEAR(t[3], 2.0f, K_EPSILON);
    ASSERT_NEAR(t[4], 5.0f, K_EPSILON);
    ASSERT_NEAR(t[5], 8.0f, K_EPSILON);
    ASSERT_NEAR(t[6], 3.0f, K_EPSILON);
    ASSERT_NEAR(t[7], 6.0f, K_EPSILON);
    ASSERT_NEAR(t[8], 9.0f, K_EPSILON);
    return 1;
}

TEST(shape_half_extents_symmetric)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t idx = fixture_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                             vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(idx >= 0);
    VoxelObject *obj = &world->objects[idx];

    printf("(half_extents=%.3f,%.3f,%.3f) ", obj->shape_half_extents.x,
           obj->shape_half_extents.y, obj->shape_half_extents.z);

    /* Symmetric box should have roughly equal half extents */
    ASSERT(fabsf(obj->shape_half_extents.x - obj->shape_half_extents.y) < 0.1f);
    ASSERT(fabsf(obj->shape_half_extents.y - obj->shape_half_extents.z) < 0.1f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(bounding_sphere_accuracy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t idx = fixture_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                             vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(idx >= 0);
    VoxelObject *obj = &world->objects[idx];

    float max_extent = obj->shape_half_extents.x;
    if (obj->shape_half_extents.y > max_extent)
        max_extent = obj->shape_half_extents.y;
    if (obj->shape_half_extents.z > max_extent)
        max_extent = obj->shape_half_extents.z;
    float expected_radius = sqrtf(3.0f) * max_extent;

    printf("(radius=%.3f, expected=%.3f) ", obj->radius, expected_radius);

    ASSERT(obj->radius >= max_extent);
    ASSERT(fabsf(obj->radius - expected_radius) < 0.1f);

    voxel_object_world_destroy(world);
    return 1;
}

static int32_t create_dumbbell_object(VoxelObjectWorld *world, Vec3 position, uint8_t material)
{
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;

    int32_t slot = world->object_count++;
    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;
    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = (float)x - half_grid + 0.5f;
                float dy = (float)y - half_grid + 0.5f;
                float dz = (float)z - half_grid + 0.5f;

                float dist_left = sqrtf((dx + 4.0f) * (dx + 4.0f) + dy * dy + dz * dz);
                float dist_right = sqrtf((dx - 4.0f) * (dx - 4.0f) + dy * dy + dz * dz);
                bool is_bridge = (fabsf(dx) <= 1.0f && fabsf(dy) <= 0.5f && fabsf(dz) <= 0.5f);

                int32_t idx = vobj_index(x, y, z);
                if (dist_left <= 3.0f || dist_right <= 3.0f || is_bridge)
                {
                    obj->voxels[idx].material = material;
                    obj->voxel_count++;
                }
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

TEST(object_split_creates_fragments)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = create_dumbbell_object(world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    VoxelObject *obj = &world->objects[obj_idx];
    int32_t initial_voxel_count = obj->voxel_count;
    int32_t initial_object_count = world->object_count;

    printf("(initial: %d voxels, %d objects) ", initial_voxel_count, initial_object_count);

    ASSERT(initial_voxel_count > 50);
    ASSERT(initial_object_count == 1);

    Vec3 bridge_point = vec3_create(0.0f, 10.0f, 0.0f);
    Vec3 destroyed_pos[64];
    uint8_t destroyed_mat[64];

    int32_t destroyed = detach_object_at_point(world, obj_idx, bridge_point, 0.5f,
                                               destroyed_pos, destroyed_mat, 64);

    /* Process deferred splits until queue is empty */
    for (int32_t tick = 0; tick < 16; tick++)
    {
        voxel_object_world_process_splits(world);
        voxel_object_world_process_recalcs(world);
    }

    printf("(destroyed %d voxels, now %d objects) ", destroyed, world->object_count);

    ASSERT(world->object_count >= 2);

    int32_t total_voxels = 0;
    for (int32_t i = 0; i < world->object_count; i++)
    {
        if (world->objects[i].active)
        {
            ASSERT(world->objects[i].voxel_count > 0);
            total_voxels += world->objects[i].voxel_count;
        }
    }

    ASSERT(total_voxels == initial_voxel_count - destroyed);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(object_split_dumbbell)
{
    /* Verify a dumbbell-shaped object splits when bridge is destroyed */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = create_dumbbell_object(world, vec3_create(0.0f, 20.0f, 0.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    Vec3 bridge_point = vec3_create(0.0f, 20.0f, 0.0f);
    detach_object_at_point(world, obj_idx, bridge_point, 0.5f, NULL, NULL, 0);

    /* Process deferred splits until queue is empty */
    for (int32_t tick = 0; tick < 16; tick++)
    {
        voxel_object_world_process_splits(world);
        voxel_object_world_process_recalcs(world);
    }

    /* Should split into 2 separate objects */
    ASSERT(world->object_count >= 2);

    /* Both fragments should be active with voxels */
    ASSERT(world->objects[0].active);
    ASSERT(world->objects[1].active);
    ASSERT(world->objects[0].voxel_count > 0);
    ASSERT(world->objects[1].voxel_count > 0);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(object_slot_no_double_free)
{
    /* Partial hit (object goes on the dirty list), then a hit that empties it
     * (detach frees the slot), then the deferred recalc pass (which used to free
     * the same slot again). The next two allocations must get distinct slots. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    Vec3 center = vec3_create(0.0f, 10.0f, 0.0f);
    int32_t idx = fixture_add_box(world, center, vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(idx >= 0);
    int32_t initial = world->objects[idx].voxel_count;

    Vec3 positions[64];
    uint8_t materials[64];
    int32_t partial = detach_object_at_point(world, idx, vec3_create(0.375f, 10.375f, 0.375f), 0.2f,
                                             positions, materials, 64);
    ASSERT(partial > 0 && partial < initial);
    ASSERT(world->objects[idx].shape_dirty);

    detach_object_at_point(world, idx, center, 10.0f, positions, materials, 64);
    ASSERT(!world->objects[idx].active);

    for (int32_t tick = 0; tick < 4; tick++)
    {
        voxel_object_world_process_splits(world);
        voxel_object_world_process_recalcs(world);
    }

    int32_t a = fixture_add_box(world, vec3_create(-3.0f, 10.0f, 0.0f),
                                           vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t b = fixture_add_box(world, vec3_create(3.0f, 10.0f, 0.0f),
                                           vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    printf("(slots %d,%d) ", a, b);
    ASSERT(a >= 0 && b >= 0);
    ASSERT(a != b);
    ASSERT(world->objects[a].active && world->objects[b].active);
    ASSERT(world->first_free_slot < 0);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(detach_counts_voxels_beyond_output_cap)
{
    /* The return value (and the revision bump that depends on it) used to be
     * capped at max_output, so a caller passing no output arrays saw 0. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t idx = fixture_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                             vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(idx >= 0);
    VoxelObject *obj = &world->objects[idx];
    int32_t before = obj->voxel_count;
    uint32_t revision = obj->voxel_revision;

    int32_t destroyed = detach_object_at_point(world, idx, vec3_create(0.875f, 10.875f, 0.875f), 0.6f,
                                               NULL, NULL, 0);
    printf("(destroyed=%d, removed=%d) ", destroyed, before - obj->voxel_count);
    ASSERT(destroyed > 1);
    ASSERT(destroyed == before - obj->voxel_count);
    ASSERT(obj->voxel_revision != revision);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(object_raycast_hit)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = fixture_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    Vec3 origin = vec3_create(0.0f, 20.0f, 0.0f);
    Vec3 dir = vec3_create(0.0f, -1.0f, 0.0f);

    VoxelObjectHit hit = voxel_object_world_raycast(world, origin, dir);

    printf("(hit=%d, obj=%d) ", hit.hit, hit.object_index);

    ASSERT(hit.hit == true);
    ASSERT(hit.object_index == obj_idx);
    ASSERT(hit.impact_point.y < origin.y);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(object_raycast_miss)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    fixture_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                               vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);

    Vec3 origin = vec3_create(10.0f, 20.0f, 0.0f);
    Vec3 dir = vec3_create(0.0f, 1.0f, 0.0f);

    VoxelObjectHit hit = voxel_object_world_raycast(world, origin, dir);

    ASSERT(hit.hit == false);

    voxel_object_world_destroy(world);
    return 1;
}

static Quat random_orientation(RngState *rng);

static int32_t count_raycast_mismatches(VoxelObjectWorld *world, RngState *rng, int32_t ray_count,
                                        int32_t *out_hits)
{
    int32_t mismatches = 0;
    for (int32_t r = 0; r < ray_count; r++)
    {
        Vec3 origin = vec3_create(rng_range_f32(rng, -25.0f, 25.0f), rng_range_f32(rng, 0.0f, 30.0f),
                                  rng_range_f32(rng, -25.0f, 25.0f));
        Vec3 dir = vec3_create(signed_half(rng), signed_half(rng), signed_half(rng));
        if (vec3_length(dir) < 1e-3f)
            dir = vec3_create(0.0f, -1.0f, 0.0f);
        dir = vec3_normalize(dir);

        VoxelObjectHit with_bvh = voxel_object_world_raycast(world, origin, dir);
        BVH *saved = world->bvh;
        world->bvh = NULL;
        VoxelObjectHit linear = voxel_object_world_raycast(world, origin, dir);
        world->bvh = saved;

        /* Objects may overlap, so with equal hit distances either object is a
         * correct answer: compare hit and nearest impact point, not the index. */
        if (with_bvh.hit != linear.hit ||
            (linear.hit && vec3_length(vec3_sub(with_bvh.impact_point, linear.impact_point)) > 1e-4f))
            mismatches++;
        if (linear.hit)
            (*out_hits)++;

        VoxelObjectPointTest pt_bvh = voxel_object_world_test_point(world, origin);
        world->bvh = NULL;
        VoxelObjectPointTest pt_linear = voxel_object_world_test_point(world, origin);
        world->bvh = saved;
        if (pt_bvh.hit != pt_linear.hit)
            mismatches++;
    }
    return mismatches;
}

TEST(object_bvh_matches_linear_queries)
{
    /* The object BVH was never built, so raycasts and point tests stayed linear. Once
     * it is kept up to date it must give exactly the linear answers: after a build, a
     * refit (objects moved) and a rebuild (objects freed and added). */
    Bounds3D bounds = {-32.0f, 32.0f, 0.0f, 64.0f, -32.0f, 32.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);
    RngState rng;
    rng_seed(&rng, 4242);

    for (int32_t i = 0; i < 150; i++)
    {
        Vec3 pos = vec3_create(rng_range_f32(&rng, -20.0f, 20.0f), rng_range_f32(&rng, 2.0f, 25.0f),
                               rng_range_f32(&rng, -20.0f, 20.0f));
        Vec3 he = vec3_create(rng_range_f32(&rng, 0.25f, 1.0f), rng_range_f32(&rng, 0.25f, 1.0f),
                              rng_range_f32(&rng, 0.25f, 1.0f));
        int32_t idx = fixture_add_box(world, pos, he, MAT_STONE);
        ASSERT(idx >= 0);
        world->objects[idx].orientation = random_orientation(&rng);
    }

    int32_t hits = 0, mismatches = 0;
    voxel_object_world_update_bvh(world);
    ASSERT(world->bvh->node_count > 0);
    mismatches += count_raycast_mismatches(world, &rng, 7000, &hits);

    for (int32_t i = 0; i < world->object_count; i++)
        world->objects[i].position = vec3_add(world->objects[i].position,
                                              vec3_create(signed_half(&rng) * 4.0f, signed_half(&rng) * 4.0f,
                                                          signed_half(&rng) * 4.0f));
    voxel_object_world_update_bvh(world);
    mismatches += count_raycast_mismatches(world, &rng, 7000, &hits);

    for (int32_t i = 0; i < 20; i++)
        voxel_object_world_free_slot(world, i * 7);
    for (int32_t i = 0; i < 30; i++)
        fixture_add_box(world, vec3_create(rng_range_f32(&rng, -20.0f, 20.0f), rng_range_f32(&rng, 2.0f, 25.0f),
                                                      rng_range_f32(&rng, -20.0f, 20.0f)),
                                   vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    voxel_object_world_update_bvh(world);
    mismatches += count_raycast_mismatches(world, &rng, 6000, &hits);

    printf("(rays=20000, hits=%d, mismatches=%d) ", hits, mismatches);
    ASSERT(hits > 1000);
    ASSERT(mismatches == 0);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(quat_rotate_vec3_y_axis)
{
    Quat q = axis_angle(vec3_create(0.0f, 1.0f, 0.0f), K_PI * 0.5f);
    Vec3 v = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 rotated = quat_rotate_vec3(q, v);
    ASSERT_NEAR(rotated.x, 0.0f, 0.01f);
    ASSERT_NEAR(rotated.y, 0.0f, 0.01f);
    ASSERT_NEAR(rotated.z, -1.0f, 0.01f);
    return 1;
}

TEST(vec3_clamp_length_under_limit)
{
    Vec3 v = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 clamped = vec3_clamp_length(v, 5.0f);
    ASSERT_NEAR(vec3_length(clamped), 1.0f, K_EPSILON);
    return 1;
}

TEST(vec3_clamp_length_over_limit)
{
    Vec3 v = vec3_create(10.0f, 0.0f, 0.0f);
    Vec3 clamped = vec3_clamp_length(v, 5.0f);
    ASSERT_NEAR(vec3_length(clamped), 5.0f, K_EPSILON);
    return 1;
}

TEST(physics_world_create_destroy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(obj_world != NULL);

    PhysicsWorld *physics = physics_world_create(obj_world, NULL);
    ASSERT(physics != NULL);
    ASSERT(physics->max_body_index == -1);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_body_add_remove)
{
    /* Bodies follow the object world: sync adds one per object, a second add returns the
     * same body, and freeing the object drops it at the next sync. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    physics_world_sync_objects(physics);
    int32_t body_idx = physics_world_find_body_for_object(physics, obj_idx);
    ASSERT(body_idx >= 0);
    ASSERT(physics_world_add_body(physics, obj_idx) == body_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body != NULL);
    ASSERT((body->flags & PHYS_FLAG_ACTIVE) != 0);
    ASSERT(body->inv_mass > 0.0f);

    voxel_object_world_free_slot(obj_world, obj_idx);
    physics_world_sync_objects(physics);
    ASSERT(physics_world_find_body_for_object(physics, obj_idx) < 0);
    ASSERT(physics_world_get_body(physics, body_idx) == NULL);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_body_inertia_symmetric)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body != NULL);

    /* A cube: the same inverse inertia about every axis */
    Vec3 inv_i = body->inv_inertia_local;
    ASSERT(inv_i.x > 0.0f);
    ASSERT(fabsf(inv_i.x - inv_i.y) < 1e-4f * inv_i.x);
    ASSERT(fabsf(inv_i.y - inv_i.z) < 1e-4f * inv_i.x);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_impulse_rotated_inertia)
{
    /* Angular response to an impulse must use the world inverse inertia
     * R * diag(1/I) * R^T. A rotated, anisotropic body exposes R^T * diag * R. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                 vec3_create(1.0f, 0.25f, 0.5f), MAT_STONE);
    ASSERT(obj_idx >= 0);
    VoxelObject *obj = &obj_world->objects[obj_idx];
    obj->orientation = axis_angle(vec3_create(0.3f, 0.4f, 0.866f), K_PI * 0.25f);
    obj->orientation = quat_normalize(obj->orientation);

    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body != NULL);
    ASSERT(fabsf(body->inv_inertia_local.x - body->inv_inertia_local.y) > 0.1f * body->inv_inertia_local.y);

    Vec3 r = vec3_create(0.2f, 0.7f, -0.1f);
    Vec3 impulse = vec3_create(0.0f, 0.0f, 3.0f);
    rigid_body_add_impulse(body, obj->orientation, impulse, r);

    /* Expected: w = R * diag(inv_I) * R^T * (r x J), with R from quat_to_mat3 (row-major). */
    float m[9];
    quat_to_mat3(obj->orientation, m);
    Vec3 L = vec3_cross(r, impulse);
    float local[3], scaled[3], expected[3];
    for (int32_t i = 0; i < 3; i++)
        local[i] = m[0 * 3 + i] * L.x + m[1 * 3 + i] * L.y + m[2 * 3 + i] * L.z;
    scaled[0] = local[0] * body->inv_inertia_local.x;
    scaled[1] = local[1] * body->inv_inertia_local.y;
    scaled[2] = local[2] * body->inv_inertia_local.z;
    for (int32_t i = 0; i < 3; i++)
        expected[i] = m[i * 3 + 0] * scaled[0] + m[i * 3 + 1] * scaled[1] + m[i * 3 + 2] * scaled[2];

    Vec3 w = body->angular_velocity;
    Vec3 diff = vec3_sub(w, vec3_create(expected[0], expected[1], expected[2]));
    float rel_error = vec3_length(diff) / vec3_length(vec3_create(expected[0], expected[1], expected[2]));
    printf("(rel_error=%.4f) ", rel_error);
    ASSERT(rel_error < 1e-3f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_spin_about_center_of_mass)
{
    /* Velocities and impulses are defined at the centre of mass, so a free body
     * spinning with zero linear velocity must keep its COM in place. Integrating
     * the orientation about the grid centre instead made asymmetric debris orbit. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);
    physics->gravity = vec3_zero();

    int32_t idx = voxel_object_world_alloc_slot(obj_world);
    ASSERT(idx >= 0);
    VoxelObject *obj = &obj_world->objects[idx];
    memset(obj, 0, sizeof(VoxelObject));
    obj->position = vec3_create(0.0f, 20.0f, 0.0f);
    obj->orientation = quat_identity();
    obj->voxel_size = obj_world->voxel_size;
    obj->active = true;
    for (int32_t z = 12; z < 20; z++)
        for (int32_t y = 12; y < 20; y++)
            for (int32_t x = 12; x < 20; x++)
            {
                if (y >= 14 && x >= 14)
                    continue; /* L-shape: COM is well off the grid centre */
                obj->voxels[vobj_index(x, y, z)].material = MAT_STONE;
                obj->voxel_count++;
            }
    voxel_object_recalc_shape(obj);
    ASSERT(vec3_length(obj->local_com) > 0.2f);

    int32_t body_idx = physics_world_add_body(physics, idx);
    ASSERT(body_idx >= 0);
    physics_body_set_angular_velocity(physics, body_idx, vec3_create(0.0f, 0.0f, 4.0f));

    Vec3 com_start = vec3_add(obj->position, quat_rotate_vec3(obj->orientation, obj->local_com));
    for (int32_t tick = 0; tick < 60; tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    Vec3 com_end = vec3_add(obj->position, quat_rotate_vec3(obj->orientation, obj->local_com));

    float drift = vec3_length(vec3_sub(com_end, com_start));
    printf("(com_offset=%.3f, com_drift=%.4f) ", vec3_length(obj->local_com), drift);
    ASSERT(drift < 0.01f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_integration_gravity)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    VoxelObject *obj = &obj_world->objects[obj_idx];
    float initial_y = obj->position.y;

    physics_world_step(physics, 1.0f / 60.0f);

    ASSERT(obj->position.y < initial_y);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body->velocity.y < 0.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_sleep_from_rest)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT((body->flags & PHYS_FLAG_SLEEPING) == 0);

    body->velocity = vec3_zero();
    body->angular_velocity = vec3_zero();
    body->sleep_frames = PHYS_SLEEP_FRAMES;
    body->flags |= PHYS_FLAG_SLEEPING;

    physics_body_wake(physics, body_idx);
    ASSERT((body->flags & PHYS_FLAG_SLEEPING) == 0);
    ASSERT(body->sleep_frames == 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_velocity_clamping)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    physics_body_set_velocity(physics, body_idx, vec3_create(100.0f, 100.0f, 100.0f));

    physics_world_step(physics, 1.0f / 60.0f);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    float speed = vec3_length(body->velocity);
    ASSERT(speed <= PHYS_MAX_LINEAR_VELOCITY + 1.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_energy_decreases)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    physics_body_set_velocity(physics, body_idx, vec3_create(5.0f, 0.0f, 0.0f));
    physics_body_set_angular_velocity(physics, body_idx, vec3_create(0.0f, 2.0f, 0.0f));

    RigidBody *body = physics_world_get_body(physics, body_idx);
    const VoxelObject *obj = &obj_world->objects[obj_idx];
    float initial_ke = 0.5f * obj->total_mass * vec3_length_sq(body->velocity);
    initial_ke += 0.5f * vec3_dot(obj->inertia_diag, vec3_mul(body->angular_velocity, body->angular_velocity));

    for (int32_t i = 0; i < 60; i++)
    {
        body->velocity.y = 0.0f;
        physics_world_step(physics, 1.0f / 60.0f);
    }

    float final_ke = 0.5f * obj->total_mass * vec3_length_sq(body->velocity);
    final_ke += 0.5f * vec3_dot(obj->inertia_diag, vec3_mul(body->angular_velocity, body->angular_velocity));

    ASSERT(final_ke < initial_ke);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_zero_mass_guard)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body->inv_mass > 0.0f);
    ASSERT(body->inv_mass < 1e6f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

/* Terrain filled with stone from the bottom of the bounds up to floor_top */
static VoxelVolume *create_floor_terrain(Bounds3D bounds, float voxel_size, float floor_top)
{
    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);
    fixture_fill_box(terrain, origin, vec3_create(bounds.max_x, floor_top, bounds.max_z), MAT_STONE);
    volume_rebuild_all_occupancy(terrain);
    return terrain;
}

TEST(physics_terrain_contact)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    VoxelVolume *terrain = create_floor_terrain(bounds, voxel_size, bounds.min_y + 0.5f);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = fixture_add_box(obj_world,
                                                 vec3_create(0.0f, floor_surface + 0.6f, 0.0f),
                                                 vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    VoxelObject *obj = &obj_world->objects[obj_idx];

    for (int32_t tick = 0; tick < 60; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    ASSERT(obj->position.y < floor_surface + 1.0f);
    ASSERT(obj->position.y > floor_surface - 0.5f);

    printf("(y=%.2f, grnd=%d) ", obj->position.y, (body->flags & PHYS_FLAG_GROUNDED) ? 1 : 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(bodies_and_debris_pass_water_and_flora)
{
    /* Stone up to 0.5 m; on its -x half a pool 1 m deep, on its +x half 0.3 m of flowers.
     * Neither water nor flora is solid: a box dropped into the pool sinks to its bed, one
     * dropped on the flowers comes to rest on the stone among them, and debris falls
     * through both alike */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    const float floor_surface = 0.5f;
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, floor_surface);
    fixture_fill_box(terrain, vec3_create(-5.0f, floor_surface, -5.0f), vec3_create(0.0f, 1.5f, 5.0f), MAT_WATER);
    fixture_fill_box(terrain, vec3_create(0.0f, floor_surface, -5.0f), vec3_create(5.0f, 0.8f, 5.0f), MAT_FLOWER_RED);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);
    ParticleSystem *debris = particle_system_create(bounds);
    ASSERT(obj_world && physics && debris);
    const Vec3 half = vec3_create(0.3f, 0.3f, 0.3f);
    int32_t in_pool = fixture_add_box(obj_world, vec3_create(-2.5f, 3.0f, 0.0f), half, MAT_STONE);
    int32_t on_flowers = fixture_add_box(obj_world, vec3_create(2.5f, 3.0f, 0.0f), half, MAT_STONE);
    ASSERT(physics_world_add_body(physics, in_pool) >= 0);
    ASSERT(physics_world_add_body(physics, on_flowers) >= 0);
    for (int32_t i = 0; i < 20; i++)
    {
        float z = -1.0f + 0.1f * (float)i;
        particle_system_add(debris, vec3_create(-2.5f, 3.0f, z), vec3_zero(), vec3_create(1.0f, 1.0f, 1.0f), 0.04f);
        particle_system_add(debris, vec3_create(2.5f, 3.0f, z), vec3_zero(), vec3_create(1.0f, 1.0f, 1.0f), 0.04f);
    }

    for (int32_t tick = 0; tick < 300; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        particle_system_update(debris, 1.0f / 60.0f, terrain, NULL);
    }

    /* Resting on the stone the box's centre is its half extent above it: on the water it
     * would be a metre higher, on the flowers 0.3 m */
    const float pool_y = obj_world->objects[in_pool].position.y;
    const float flowers_y = obj_world->objects[on_flowers].position.y;
    float highest_debris = 0.0f;
    int32_t debris_alive = 0;
    for (int32_t i = 0; i < debris->count; i++)
        if (debris->particles[i].active)
        {
            debris_alive++;
            highest_debris = fmaxf(highest_debris, debris->particles[i].position.y);
        }
    printf("(box in pool %.2f, on flowers %.2f, highest debris %.2f) ", pool_y, flowers_y, highest_debris);
    ASSERT_EQ(debris_alive, 40);
    ASSERT(pool_y < floor_surface + half.y + 0.15f);
    ASSERT(flowers_y < floor_surface + half.y + 0.15f);
    ASSERT(highest_debris < floor_surface + 0.15f);

    particle_system_destroy(debris);
    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_terrain_settling)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    VoxelVolume *terrain = create_floor_terrain(bounds, voxel_size, bounds.min_y + 0.5f);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = fixture_add_box(obj_world,
                                                 vec3_create(0.0f, floor_surface + 3.0f, 0.0f),
                                                 vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    VoxelObject *obj = &obj_world->objects[obj_idx];

    int32_t settled_tick = -1;
    int32_t first_grounded = -1;
    int32_t max_sleep_frames = 0;
    int32_t above_threshold_count = 0;

    for (int32_t tick = 0; tick < 900; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);

        if (first_grounded < 0 && (body->flags & PHYS_FLAG_GROUNDED))
            first_grounded = tick;

        if (body->sleep_frames > max_sleep_frames)
            max_sleep_frames = body->sleep_frames;

        float lin = vec3_length(body->velocity);
        float ang = vec3_length(body->angular_velocity);
        if (lin >= 0.05f || ang >= 0.1f)
            above_threshold_count++;

        if (body->flags & PHYS_FLAG_SLEEPING)
        {
            settled_tick = tick;
            break;
        }
    }

    printf("(y=%.2f, vel=%.3f, grnd=%d, sleep=%d, max_frames=%d, above=%d) ",
           obj->position.y, vec3_length(body->velocity),
           first_grounded, settled_tick, max_sleep_frames, above_threshold_count);

    ASSERT(first_grounded >= 0);
    ASSERT(settled_tick >= 0);
    ASSERT((body->flags & PHYS_FLAG_SLEEPING) != 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_no_jitter_after_sleep)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    VoxelVolume *terrain = create_floor_terrain(bounds, voxel_size, bounds.min_y + 0.5f);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = fixture_add_box(obj_world,
                                                 vec3_create(0.0f, floor_surface + 0.5f, 0.0f),
                                                 vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    VoxelObject *obj = &obj_world->objects[obj_idx];

    for (int32_t tick = 0; tick < 900; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        if (body->flags & PHYS_FLAG_SLEEPING)
            break;
    }

    if (!(body->flags & PHYS_FLAG_SLEEPING))
    {
        printf("(NEVER SLEPT: y=%.2f, vel=%.3f,%.3f,%.3f, grnd=%d) ",
               obj->position.y, body->velocity.x, body->velocity.y, body->velocity.z,
               (body->flags & PHYS_FLAG_GROUNDED) ? 1 : 0);
        ASSERT(0);
    }

    Vec3 sleep_pos = obj->position;

    for (int32_t tick = 0; tick < 120; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    Vec3 after_pos = obj->position;
    float drift = vec3_length(vec3_sub(after_pos, sleep_pos));

    printf("(drift=%.4f) ", drift);

    ASSERT(drift < 0.001f);
    ASSERT((body->flags & PHYS_FLAG_SLEEPING) != 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_performance_128_bodies)
{
    Bounds3D bounds = {-64.0f, 64.0f, 0.0f, 128.0f, -64.0f, 64.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t created = 0;
    for (int32_t i = 0; i < 128; i++)
    {
        float x = (float)(i % 8) * 4.0f - 14.0f;
        float y = 20.0f + (float)(i / 64) * 4.0f;
        float z = (float)((i / 8) % 8) * 4.0f - 14.0f;

        int32_t obj_idx = fixture_add_box(obj_world, vec3_create(x, y, z),
                                                     vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
        if (obj_idx >= 0 && physics_world_add_body(physics, obj_idx) >= 0)
            created++;
    }

    printf("(created %d bodies) ", created);
    ASSERT(created == 128);

    PlatformTime t0 = platform_time_now();

    for (int32_t tick = 0; tick < 1000; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    PlatformTime t1 = platform_time_now();
    float elapsed_ms = platform_time_delta_seconds(t0, t1) * 1000.0f;

    printf("(1000 ticks in %.1fms) ", elapsed_ms);

    ASSERT_PERF(elapsed_ms < 500.0f * PERF_TIME_SCALE);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_static_flag)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    body->flags |= PHYS_FLAG_STATIC;

    VoxelObject *obj = &obj_world->objects[obj_idx];
    float initial_y = obj->position.y;

    for (int32_t i = 0; i < 60; i++)
        physics_world_step(physics, 1.0f / 60.0f);

    ASSERT_NEAR(obj->position.y, initial_y, 0.01f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_basic)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_a = fixture_add_box(obj_world, vec3_create(-0.3f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = fixture_add_box(obj_world, vec3_create(0.3f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    ASSERT(physics_world_add_body(physics, obj_a) >= 0);
    ASSERT(physics_world_add_body(physics, obj_b) >= 0);

    Vec3 initial_pos_a = obj_world->objects[obj_a].position;
    Vec3 initial_pos_b = obj_world->objects[obj_b].position;

    for (int32_t tick = 0; tick < 30; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    Vec3 final_pos_a = obj_world->objects[obj_a].position;
    Vec3 final_pos_b = obj_world->objects[obj_b].position;

    float initial_dist = fabsf(initial_pos_b.x - initial_pos_a.x);
    float final_dist = fabsf(final_pos_b.x - final_pos_a.x);

    printf("(initial_dist=%.2f, final_dist=%.2f) ", initial_dist, final_dist);

    /* Two 1 m boxes starting 0.4 m into each other: the contact pushes them apart to
     * touching. Without it they would fall together, still 0.6 m apart. */
    ASSERT(final_dist >= 0.95f);
    ASSERT(final_dist < 1.1f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

static int32_t create_small_box_object(VoxelObjectWorld *world, Vec3 position, uint8_t material)
{
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;

    int32_t slot = world->object_count++;
    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;
    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    int32_t lo = VOBJ_GRID_SIZE / 2 - 2;
    int32_t hi = VOBJ_GRID_SIZE / 2 + 2;

    for (int32_t z = lo; z < hi; z++)
    {
        for (int32_t y = lo; y < hi; y++)
        {
            for (int32_t x = lo; x < hi; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                obj->voxels[idx].material = material;
                obj->voxel_count++;
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

TEST(object_collision_dumbbell_gap)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t dumbbell_idx = create_dumbbell_object(obj_world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(dumbbell_idx >= 0);

    int32_t small_box = create_small_box_object(obj_world, vec3_create(0.0f, 10.0f + 1.5f, 0.0f), MAT_STONE);
    ASSERT(small_box >= 0);

    int32_t body_dumbbell = physics_world_add_body(physics, dumbbell_idx);
    int32_t body_small = physics_world_add_body(physics, small_box);
    ASSERT(body_dumbbell >= 0 && body_small >= 0);

    RigidBody *rb_dumbbell = physics_world_get_body(physics, body_dumbbell);
    rb_dumbbell->flags |= PHYS_FLAG_STATIC;

    RigidBody *rb_small = physics_world_get_body(physics, body_small);
    rb_small->velocity = vec3_create(0.0f, -2.0f, 0.0f);

    float initial_y = obj_world->objects[small_box].position.y;

    for (int32_t tick = 0; tick < 60; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    float final_y = obj_world->objects[small_box].position.y;

    printf("(initial_y=%.2f, final_y=%.2f) ", initial_y, final_y);

    ASSERT(final_y > initial_y - 2.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

static int32_t create_l_shape_object(VoxelObjectWorld *world, Vec3 position, uint8_t material)
{
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;

    int32_t slot = world->object_count++;
    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;
    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    for (int32_t z = 4; z < 12; z++)
    {
        for (int32_t y = 4; y < 12; y++)
        {
            for (int32_t x = 4; x < 8; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                obj->voxels[idx].material = material;
                obj->voxel_count++;
            }
        }
    }

    for (int32_t z = 4; z < 8; z++)
    {
        for (int32_t y = 4; y < 12; y++)
        {
            for (int32_t x = 8; x < 12; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                obj->voxels[idx].material = material;
                obj->voxel_count++;
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

TEST(object_collision_l_shape)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    /* The L's voxels sit in grid cells 4..11, so recentring moves its position to
     * (-2, 8, -2): it spans x [-3, -1], y [7, 9], z [-3, -1], with its long arm at
     * x [-3, -2]. The 1 m box starts 1.5 m above the middle of that arm. */
    int32_t l_shape = create_l_shape_object(obj_world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(l_shape >= 0);
    const VoxelObject *l_obj = &obj_world->objects[l_shape];
    float l_top = voxel_object_box_center(l_obj).y + l_obj->shape_half_extents.y;
    ASSERT_NEAR(l_top, 9.0f, 1e-4f);

    int32_t small_box = create_small_box_object(obj_world, vec3_create(-2.5f, 11.0f, -2.0f), MAT_STONE);
    ASSERT(small_box >= 0);

    int32_t body_l = physics_world_add_body(physics, l_shape);
    int32_t body_small = physics_world_add_body(physics, small_box);
    ASSERT(body_l >= 0 && body_small >= 0);

    RigidBody *rb_l = physics_world_get_body(physics, body_l);
    rb_l->flags |= PHYS_FLAG_STATIC;

    RigidBody *rb_small = physics_world_get_body(physics, body_small);
    rb_small->velocity = vec3_create(0.0f, -2.0f, 0.0f);

    int32_t contact_ticks = 0;
    for (int32_t tick = 0; tick < 60; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        ObjectCollisionPair pairs[4];
        contact_ticks += physics_detect_object_pairs(physics, pairs, 4) > 0 ? 1 : 0;
    }

    const VoxelObject *box = &obj_world->objects[small_box];
    float box_bottom = voxel_object_box_center(box).y - box->shape_half_extents.y;

    printf("(box bottom=%.3f, L top=%.3f, contact ticks=%d) ", box_bottom, l_top, contact_ticks);

    /* It lands on the arm and stays there: neither falls past nor hovers above it */
    ASSERT(contact_ticks > 10);
    ASSERT(fabsf(box_bottom - l_top) < 0.1f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_contact_accuracy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_a = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = fixture_add_box(obj_world, vec3_create(0.95f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    int32_t body_a = physics_world_add_body(physics, obj_a);
    int32_t body_b = physics_world_add_body(physics, obj_b);
    ASSERT(body_a >= 0 && body_b >= 0);

    /* One tiny step fills the broadphase; before this the pair search saw nothing */
    physics_world_step(physics, 1.0e-4f);

    ObjectCollisionPair pairs[PHYS_OBJ_COLLISION_BUDGET];
    int32_t pair_count = physics_detect_object_pairs(physics, pairs, PHYS_OBJ_COLLISION_BUDGET);

    printf("(pair_count=%d) ", pair_count);
    ASSERT(pair_count > 0);

    {
        ObjectCollisionPair *pair = &pairs[0];

        Vec3 pos_a = obj_world->objects[obj_a].position;
        Vec3 pos_b = obj_world->objects[obj_b].position;
        Vec3 expected_contact = vec3_scale(vec3_add(pos_a, pos_b), 0.5f);

        float contact_error = vec3_length(vec3_sub(pair->contact_point, expected_contact));
        printf("(contact_error=%.3f) ", contact_error);

        ASSERT(contact_error < 1.0f);

        float normal_len = vec3_length(pair->contact_normal);
        ASSERT(fabsf(normal_len - 1.0f) < 0.01f);
    }

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_moving_head_on)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_a = fixture_add_box(obj_world, vec3_create(-3.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = fixture_add_box(obj_world, vec3_create(3.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    int32_t body_a = physics_world_add_body(physics, obj_a);
    int32_t body_b = physics_world_add_body(physics, obj_b);
    ASSERT(body_a >= 0 && body_b >= 0);

    RigidBody *rb_a = physics_world_get_body(physics, body_a);
    RigidBody *rb_b = physics_world_get_body(physics, body_b);

    rb_a->velocity = vec3_create(10.0f, 0.0f, 0.0f);
    rb_b->velocity = vec3_create(-10.0f, 0.0f, 0.0f);

    for (int32_t tick = 0; tick < 120; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    Vec3 final_pos_a = obj_world->objects[obj_a].position;
    Vec3 final_pos_b = obj_world->objects[obj_b].position;
    float final_dist = fabsf(final_pos_b.x - final_pos_a.x);

    float half_ext_a = obj_world->objects[obj_a].shape_half_extents.x;
    float half_ext_b = obj_world->objects[obj_b].shape_half_extents.x;
    float min_separation = (half_ext_a + half_ext_b) * 2.0f;

    printf("(final_dist=%.3f, min_sep=%.3f, vel_a=%.2f, vel_b=%.2f) ",
           final_dist, min_separation, rb_a->velocity.x, rb_b->velocity.x);

    ASSERT(final_dist >= min_separation - 0.5f);
    ASSERT(rb_a->velocity.x <= 0.1f);
    ASSERT(rb_b->velocity.x >= -0.1f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

static int run_stacking_scenario(void)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_base = fixture_add_box(obj_world, vec3_create(0.0f, 5.0f, 0.0f),
                                                  vec3_create(1.0f, 0.5f, 1.0f), MAT_STONE);
    int32_t obj_top = fixture_add_box(obj_world, vec3_create(0.0f, 7.0f, 0.0f),
                                                 vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_base >= 0 && obj_top >= 0);

    int32_t body_base = physics_world_add_body(physics, obj_base);
    int32_t body_top = physics_world_add_body(physics, obj_top);
    ASSERT(body_base >= 0 && body_top >= 0);

    RigidBody *rb_base = physics_world_get_body(physics, body_base);
    rb_base->flags |= PHYS_FLAG_STATIC;

    VoxelObject *top_obj = &obj_world->objects[obj_top];
    float base_top_y = obj_world->objects[obj_base].position.y +
                       obj_world->objects[obj_base].shape_half_extents.y;

    for (int32_t tick = 0; tick < 300; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    float top_bottom_y = top_obj->position.y - top_obj->shape_half_extents.y;
    float gap = top_bottom_y - base_top_y;

    printf("(top_y=%.3f, base_top=%.3f, gap=%.3f) ", top_obj->position.y, base_top_y, gap);

    ASSERT(gap > -0.3f);
    ASSERT(gap < 0.5f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

/* Grid x range of the object's voxels */
static void voxel_x_range(const VoxelObject *obj, int32_t *min_x, int32_t *max_x)
{
    *min_x = VOBJ_GRID_SIZE;
    *max_x = -1;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material == 0)
            continue;
        int32_t x, y, z;
        vobj_coords(i, &x, &y, &z);
        *min_x = x < *min_x ? x : *min_x;
        *max_x = x > *max_x ? x : *max_x;
    }
}

/* Removes the voxel slice at grid x = cut_x (recalc gives the object a new revision) */
static void cut_slice_x(VoxelObject *obj, int32_t cut_x)
{
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            int32_t i = vobj_index(cut_x, y, z);
            if (obj->voxels[i].material != 0)
            {
                obj->voxels[i].material = 0;
                obj->voxel_count--;
            }
        }
    voxel_object_recalc_shape(obj);
}

TEST(split_piece_keeps_parent_velocity)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    /* 2 m bar, then cut a slice out of the middle so it splits in two */
    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 20.0f, 0.0f),
                                                 vec3_create(1.0f, 0.25f, 0.25f), MAT_STONE);
    ASSERT(obj_idx >= 0);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    Vec3 moving = vec3_create(4.0f, 2.0f, -1.0f);
    physics_body_set_velocity(physics, body_idx, moving);

    VoxelObject *obj = &obj_world->objects[obj_idx];
    int32_t min_x, max_x;
    voxel_x_range(obj, &min_x, &max_x);
    ASSERT(max_x - min_x >= 2);
    cut_slice_x(obj, (min_x + max_x) / 2);

    int32_t before = obj_world->object_count;
    voxel_object_world_queue_split(obj_world, obj_idx);
    voxel_object_world_process_splits(obj_world);
    physics_world_sync_objects(physics);

    int32_t checked = 0;
    for (int32_t i = 0; i < obj_world->object_count; i++)
    {
        if (!obj_world->objects[i].active)
            continue;
        int32_t b = physics_world_find_body_for_object(physics, i);
        ASSERT(b >= 0);
        RigidBody *rb = physics_world_get_body(physics, b);
        float err = vec3_length(vec3_sub(rb->velocity, moving));
        printf("(obj %d v_err=%.3f) ", i, err);
        ASSERT(err < 0.01f);
        checked++;
    }
    ASSERT(checked == 2);
    ASSERT(obj_world->object_count >= before);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_stacking)
{
    return run_stacking_scenario();
}

TEST(object_collision_stacking_after_world_reuse)
{
    /* Fill the hull cache for slots 0 and 1 with small boxes in a world that is then
     * destroyed. The next world is usually allocated at the same address, and its
     * objects start in the same slots, so a cache keyed on (pointer, revision) with
     * per-object revisions restarting at 1 hands back the old hulls. */
    for (int32_t round = 0; round < 2; round++)
    {
        Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
        VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
        PhysicsWorld *physics = physics_world_create(obj_world, NULL);
        int32_t a = fixture_add_box(obj_world, vec3_create(0.0f, 5.0f, 0.0f),
                                               vec3_create(0.25f, 0.25f, 0.25f), MAT_STONE);
        int32_t b = fixture_add_box(obj_world, vec3_create(0.0f, 5.4f, 0.0f),
                                               vec3_create(0.25f, 0.25f, 0.25f), MAT_STONE);
        ASSERT(a == 0 && b == 1);
        physics_world_add_body(physics, a);
        physics_world_add_body(physics, b);
        physics_world_step(physics, 1.0f / 60.0f);
        physics_world_destroy(physics);
        voxel_object_world_destroy(obj_world);

        if (!run_stacking_scenario())
            return 0;
    }
    return 1;
}

TEST(hull_cache_slot_reuse)
{
    /* A slot freed and reused within one world must not inherit the previous
     * occupant's cached convex hull. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t big = fixture_add_box(obj_world, vec3_create(0.0f, 6.45f, 0.0f),
                                             vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t base = fixture_add_box(obj_world, vec3_create(0.0f, 5.0f, 0.0f),
                                              vec3_create(1.0f, 0.5f, 1.0f), MAT_STONE);
    ASSERT(big >= 0 && base >= 0);
    ASSERT(physics_world_add_body(physics, big) >= 0);
    int32_t body_base = physics_world_add_body(physics, base);
    physics_world_get_body(physics, body_base)->flags |= PHYS_FLAG_STATIC;

    /* Overlapping pair: builds and caches the hull of the 2 m box in slot `big`. */
    physics_world_step(physics, 1.0f / 60.0f);

    voxel_object_world_free_slot(obj_world, big);
    physics_world_sync_objects(physics);

    int32_t small = fixture_add_box(obj_world, vec3_create(0.0f, 6.5f, 0.0f),
                                               vec3_create(0.25f, 0.25f, 0.25f), MAT_STONE);
    ASSERT(small == big);
    int32_t body_small = physics_world_add_body(physics, small);
    ASSERT(body_small >= 0);

    for (int32_t tick = 0; tick < 180; tick++)
        physics_world_step(physics, 1.0f / 60.0f);

    VoxelObject *obj = &obj_world->objects[small];
    float base_top = obj_world->objects[base].position.y + obj_world->objects[base].shape_half_extents.y;
    float gap = (obj->position.y - obj->shape_half_extents.y) - base_top;
    printf("(gap=%.3f) ", gap);

    /* With the stale 2 m hull the small box hovers ~0.75 m above the base. */
    ASSERT(gap > -0.2f);
    ASSERT(gap < 0.2f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_crossed_planks)
{
    /* A static plank turned 90 degrees about Y, and a second plank dropped across
     * its far end. The broadphase AABBs must follow the rotation, or the pair is
     * never tested and the top plank falls through. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    Vec3 plank_he = vec3_create(1.5f, 0.25f, 0.25f);
    int32_t lower = fixture_add_box(obj_world, vec3_create(0.0f, 5.0f, 0.0f), plank_he, MAT_STONE);
    int32_t upper = fixture_add_box(obj_world, vec3_create(0.0f, 6.0f, 1.0f), plank_he, MAT_STONE);
    ASSERT(lower >= 0 && upper >= 0);
    obj_world->objects[lower].orientation = axis_angle(vec3_create(0.0f, 1.0f, 0.0f), K_PI * 0.5f);

    int32_t body_lower = physics_world_add_body(physics, lower);
    int32_t body_upper = physics_world_add_body(physics, upper);
    ASSERT(body_lower >= 0 && body_upper >= 0);
    physics_world_get_body(physics, body_lower)->flags |= PHYS_FLAG_STATIC;

    for (int32_t tick = 0; tick < 120; tick++)
        physics_world_step(physics, 1.0f / 60.0f);

    float y = obj_world->objects[upper].position.y;
    printf("(upper_y=%.3f) ", y);
    ASSERT(y > 5.3f);
    ASSERT(y < 6.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

/* Worst gap between the true support of the object's voxels and the support of the
 * convex hull built from obj->surface_voxels, over random directions. */
static float hull_support_error(const VoxelObject *obj, RngState *rng, int32_t dir_count)
{
    static ConvexHull hull;
    convex_hull_build(obj->surface_voxels, obj->surface_voxel_count, &hull);
    if (hull.vertex_count < 4)
        return 1e30f;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float vs = obj->voxel_size;
    float worst = 0.0f;
    for (int32_t d = 0; d < dir_count; d++)
    {
        Vec3 dir = vec3_create(signed_half(rng), signed_half(rng), signed_half(rng));
        if (vec3_length(dir) < 1e-3f)
            continue;
        dir = vec3_normalize(dir);

        float true_support = -1e30f;
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            if (obj->voxels[i].material == 0)
                continue;
            int32_t x, y, z;
            vobj_coords(i, &x, &y, &z);
            Vec3 corner = vec3_create(((float)x + (dir.x > 0.0f ? 1.0f : 0.0f) - half_grid) * vs,
                                      ((float)y + (dir.y > 0.0f ? 1.0f : 0.0f) - half_grid) * vs,
                                      ((float)z + (dir.z > 0.0f ? 1.0f : 0.0f) - half_grid) * vs);
            float s = vec3_dot(corner, dir);
            if (s > true_support)
                true_support = s;
        }

        int32_t best = convex_hull_support(&hull, dir);
        float hull_support = vec3_dot(hull.vertices[best], dir);
        float err = fabsf(true_support - hull_support);
        if (err > worst)
            worst = err;
    }
    return worst;
}

TEST(hull_covers_large_objects)
{
    /* Hull points used to be the first 512 surface voxels in z-major order, so a
     * 2 m cube at 0.1 m voxels got a hull spanning only its lowest slices. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    RngState rng;
    rng_seed(&rng, 77);

    int32_t cube = fixture_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                              vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(cube >= 0);
    float cube_err = hull_support_error(&world->objects[cube], &rng, 400);

    int32_t l_idx = voxel_object_world_alloc_slot(world);
    ASSERT(l_idx >= 0);
    VoxelObject *l_obj = &world->objects[l_idx];
    memset(l_obj, 0, sizeof(VoxelObject));
    l_obj->position = vec3_create(5.0f, 10.0f, 0.0f);
    l_obj->orientation = quat_identity();
    l_obj->voxel_size = world->voxel_size;
    l_obj->active = true;
    for (int32_t z = 2; z < 30; z++)
        for (int32_t y = 2; y < 30; y++)
            for (int32_t x = 2; x < 30; x++)
            {
                if (y >= 10 && x >= 10)
                    continue;
                l_obj->voxels[vobj_index(x, y, z)].material = MAT_STONE;
                l_obj->voxel_count++;
            }
    voxel_object_recalc_shape(l_obj);
    float l_err = hull_support_error(l_obj, &rng, 400);

    printf("(cube_err=%.4f, l_err=%.4f, points=%d/%d) ", cube_err, l_err,
           world->objects[cube].surface_voxel_count, l_obj->surface_voxel_count);
    ASSERT(cube_err < 0.01f);
    ASSERT(l_err < 0.01f);

    voxel_object_world_destroy(world);
    return 1;
}

static int32_t create_wall_object(VoxelObjectWorld *world, Vec3 position, uint8_t material)
{
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;

    int32_t slot = world->object_count++;
    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;
    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    int32_t hg = VOBJ_GRID_SIZE / 2;
    for (int32_t z = hg - 4; z < hg + 4; z++)
    {
        for (int32_t y = hg - 8; y < hg + 8; y++)
        {
            for (int32_t x = hg - 2; x < hg + 2; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                obj->voxels[idx].material = material;
                obj->voxel_count++;
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

TEST(object_collision_wall_friction)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t wall_idx = create_wall_object(obj_world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(wall_idx >= 0);

    VoxelObject *wall = &obj_world->objects[wall_idx];
    float wall_right = wall->position.x + wall->shape_half_extents.x;

    int32_t box_idx = fixture_add_box(obj_world,
                                                 vec3_create(wall_right + 1.0f, 10.0f, 0.0f),
                                                 vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(box_idx >= 0);

    int32_t body_wall = physics_world_add_body(physics, wall_idx);
    int32_t body_box = physics_world_add_body(physics, box_idx);
    ASSERT(body_wall >= 0 && body_box >= 0);

    RigidBody *rb_wall = physics_world_get_body(physics, body_wall);
    RigidBody *rb_box = physics_world_get_body(physics, body_box);
    rb_wall->flags |= PHYS_FLAG_STATIC;
    rb_box->friction = 0.9f;

    rb_box->velocity = vec3_create(-5.0f, 0.0f, 0.0f);

    float initial_y = obj_world->objects[box_idx].position.y;
    float initial_x = obj_world->objects[box_idx].position.x;
    bool collision_detected = false;

    for (int32_t tick = 0; tick < 60; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);

        float current_x = obj_world->objects[box_idx].position.x;
        if (current_x < initial_x && current_x > wall_right - 0.5f)
            collision_detected = true;
    }

    float final_y = obj_world->objects[box_idx].position.y;
    float final_x = obj_world->objects[box_idx].position.x;
    float y_drop = initial_y - final_y;
    float x_vel = rb_box->velocity.x;

    printf("(y_drop=%.2f, final_x=%.2f, x_vel=%.2f, collision=%d) ",
           y_drop, final_x, x_vel, collision_detected ? 1 : 0);

    ASSERT(final_x > wall_right - 1.0f);
    ASSERT(x_vel > -2.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_no_interpenetration)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_a = fixture_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = fixture_add_box(obj_world, vec3_create(0.5f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    int32_t body_a = physics_world_add_body(physics, obj_a);
    int32_t body_b = physics_world_add_body(physics, obj_b);
    ASSERT(body_a >= 0 && body_b >= 0);

    int32_t max_interpenetration_ticks = 0;
    float max_interpenetration = 0.0f;

    for (int32_t tick = 0; tick < 120; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);

        VoxelObject *oa = &obj_world->objects[obj_a];
        VoxelObject *ob = &obj_world->objects[obj_b];
        float dist = fabsf(ob->position.x - oa->position.x);
        float min_dist = oa->shape_half_extents.x + ob->shape_half_extents.x;

        if (dist < min_dist)
        {
            float pen = min_dist - dist;
            if (pen > max_interpenetration)
            {
                max_interpenetration = pen;
                max_interpenetration_ticks = tick;
            }
        }
    }

    printf("(max_pen=%.3f at tick %d) ", max_interpenetration, max_interpenetration_ticks);

    ASSERT(max_interpenetration < 0.5f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_no_sleep_midair)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
                                                  vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    body->velocity = vec3_zero();
    body->angular_velocity = vec3_zero();

    for (int32_t tick = 0; tick < 600; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    printf("(sleeping=%d, y=%.2f) ",
           (body->flags & PHYS_FLAG_SLEEPING) ? 1 : 0,
           obj_world->objects[obj_idx].position.y);

    ASSERT((body->flags & PHYS_FLAG_SLEEPING) == 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_terrain_bounce)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    VoxelVolume *terrain = create_floor_terrain(bounds, voxel_size, bounds.min_y + 0.5f);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = fixture_add_box(obj_world,
                                                  vec3_create(0.0f, floor_surface + 3.0f, 0.0f),
                                                  vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    bool bounced = false;

    for (int32_t tick = 0; tick < 300; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);

        if ((body->flags & PHYS_FLAG_GROUNDED) && body->velocity.y > 0.1f)
        {
            bounced = true;
            break;
        }
    }

    printf("(bounced=%d) ", bounced ? 1 : 0);
    ASSERT(bounced);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_no_tunneling)
{
    Bounds3D bounds = {-10.0f, 10.0f, 0.0f, 20.0f, -10.0f, 10.0f};
    float voxel_size = 0.1f;

    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);

    Vec3 wall_min = vec3_create(-0.5f, 0.0f, -5.0f);
    Vec3 wall_max = vec3_create(0.5f, 10.0f, 5.0f);
    fixture_fill_box(terrain, wall_min, wall_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t obj_idx = fixture_add_box(obj_world,
                                                  vec3_create(-5.0f, 5.0f, 0.0f),
                                                  vec3_create(0.15f, 0.15f, 0.15f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    physics_body_set_velocity(physics, body_idx, vec3_create(20.0f, 0.0f, 0.0f));

    for (int32_t tick = 0; tick < 60; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    VoxelObject *obj = &obj_world->objects[obj_idx];
    printf("(x=%.2f) ", obj->position.x);

    ASSERT(obj->position.x < 0.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(terrain_contact_l_shape_stability)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 20.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    VoxelVolume *terrain = create_floor_terrain(bounds, voxel_size, bounds.min_y + 0.5f);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = create_l_shape_object(obj_world, vec3_create(0.0f, floor_surface + 2.0f, 0.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    VoxelObject *obj = &obj_world->objects[obj_idx];

    int32_t settled_tick = -1;
    for (int32_t tick = 0; tick < 900; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);

        if (body->flags & PHYS_FLAG_SLEEPING)
        {
            settled_tick = tick;
            break;
        }
    }

    float final_y = obj->position.y;
    bool above_floor = final_y > floor_surface - 0.5f;
    bool below_start = final_y < floor_surface + 2.0f;

    printf("(settled=%d, y=%.2f) ", settled_tick, final_y);

    ASSERT(settled_tick >= 0);
    ASSERT(above_floor);
    ASSERT(below_start);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

static void build_box_hull(Vec3 he, ConvexHull *hull)
{
    Vec3 corners[8];
    for (int32_t c = 0; c < 8; c++)
        corners[c] = vec3_create((c & 1) ? he.x : -he.x, (c & 2) ? he.y : -he.y, (c & 4) ? he.z : -he.z);
    convex_hull_build(corners, 8, hull);
    hull->margin = 0.0f;
}

static Quat random_orientation(RngState *rng)
{
    Vec3 axis = vec3_create(signed_half(rng), signed_half(rng), signed_half(rng));
    if (vec3_length(axis) < 1e-3f)
        axis = vec3_create(0.0f, 1.0f, 0.0f);
    return axis_angle(vec3_normalize(axis), rng_float(rng) * 2.0f * K_PI);
}

/* Exact OBB-OBB penetration depth over the 15 SAT axes (negative = separated by at least -depth). */
static float obb_sat_depth(Vec3 pos_a, Quat rot_a, Vec3 he_a, Vec3 pos_b, Quat rot_b, Vec3 he_b)
{
    Vec3 axes_a[3], axes_b[3], axes[15];
    for (int32_t i = 0; i < 3; i++)
    {
        Vec3 unit = vec3_create(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f);
        axes_a[i] = quat_rotate_vec3(rot_a, unit);
        axes_b[i] = quat_rotate_vec3(rot_b, unit);
    }
    int32_t n = 0;
    for (int32_t i = 0; i < 3; i++)
        axes[n++] = axes_a[i];
    for (int32_t i = 0; i < 3; i++)
        axes[n++] = axes_b[i];
    for (int32_t i = 0; i < 3; i++)
        for (int32_t j = 0; j < 3; j++)
            axes[n++] = vec3_cross(axes_a[i], axes_b[j]);

    float ea[3] = {he_a.x, he_a.y, he_a.z};
    float eb[3] = {he_b.x, he_b.y, he_b.z};
    Vec3 d = vec3_sub(pos_b, pos_a);
    float depth = 1e30f;
    for (int32_t k = 0; k < 15; k++)
    {
        float len = vec3_length(axes[k]);
        if (len < 1e-4f)
            continue;
        Vec3 axis = vec3_scale(axes[k], 1.0f / len);
        float ra = 0.0f, rb = 0.0f;
        for (int32_t i = 0; i < 3; i++)
        {
            ra += ea[i] * fabsf(vec3_dot(axes_a[i], axis));
            rb += eb[i] * fabsf(vec3_dot(axes_b[i], axis));
        }
        float overlap = ra + rb - fabsf(vec3_dot(d, axis));
        if (overlap < depth)
            depth = overlap;
    }
    return depth;
}

TEST(gjk_random_box_pairs)
{
    /* Regression for the tetrahedron ACD case in gjk.c, which kept the wrong
     * triangle: ~0.1% of overlapping box pairs were reported as separated. */
    RngState rng;
    rng_seed(&rng, 0x5EED);
    ConvexHull hull_a, hull_b;
    int32_t overlapping = 0, false_negatives = 0, separated = 0, false_positives = 0;

    for (int32_t i = 0; i < 20000; i++)
    {
        Vec3 he_a = vec3_create(rng_range_f32(&rng, 0.05f, 1.0f), rng_range_f32(&rng, 0.05f, 1.0f),
                                rng_range_f32(&rng, 0.05f, 1.0f));
        Vec3 he_b = vec3_create(rng_range_f32(&rng, 0.05f, 1.0f), rng_range_f32(&rng, 0.05f, 1.0f),
                                rng_range_f32(&rng, 0.05f, 1.0f));
        Quat rot_a = random_orientation(&rng);
        Quat rot_b = random_orientation(&rng);
        Vec3 pos_a = vec3_create(rng_range_f32(&rng, -5.0f, 5.0f), rng_range_f32(&rng, -5.0f, 5.0f),
                                 rng_range_f32(&rng, -5.0f, 5.0f));
        Vec3 dir = vec3_create(signed_half(&rng), signed_half(&rng), signed_half(&rng));
        if (vec3_length(dir) < 1e-3f)
            dir = vec3_create(1.0f, 0.0f, 0.0f);
        float reach = vec3_length(he_a) + vec3_length(he_b);
        Vec3 pos_b = vec3_add(pos_a, vec3_scale(vec3_normalize(dir), rng_float(&rng) * reach));

        build_box_hull(he_a, &hull_a);
        build_box_hull(he_b, &hull_b);
        float depth = obb_sat_depth(pos_a, rot_a, he_a, pos_b, rot_b, he_b);
        GJKSimplex simplex;
        bool hit = gjk_intersect(&hull_a, pos_a, rot_a, &hull_b, pos_b, rot_b, &simplex);

        if (depth > 1e-3f)
        {
            overlapping++;
            if (!hit)
                false_negatives++;
        }
        else if (depth < -1e-3f)
        {
            separated++;
            if (hit)
                false_positives++;
        }
    }

    printf("(overlap=%d miss=%d, separated=%d false_hit=%d) ", overlapping, false_negatives,
           separated, false_positives);
    ASSERT(overlapping > 10000);
    ASSERT(false_negatives == 0);
    ASSERT(false_positives == 0);
    return 1;
}

/* Linear plus rotational kinetic energy; the body-frame inertia makes it exact for any
 * orientation */
static float kinetic_energy(const RigidBody *body, const VoxelObject *obj)
{
    Vec3 w = quat_rotate_vec3(quat_conjugate(obj->orientation), body->angular_velocity);
    Vec3 inertia = obj->inertia_diag;
    float spin = inertia.x * w.x * w.x + inertia.y * w.y * w.y + inertia.z * w.z * w.z;
    return 0.5f * (obj->total_mass * vec3_length_sq(body->velocity) + spin);
}

TEST(physics_damping_independent_of_substeps)
{
    /* Damping factors were applied once per substep, so a body fast enough to be
     * substepped lost speed several times faster than a slow one. */
    Bounds3D bounds = {-64.0f, 64.0f, 0.0f, 64.0f, -64.0f, 64.0f};
    float ratio[2];
    float speeds[2] = {5.0f, 25.0f};
    for (int32_t k = 0; k < 2; k++)
    {
        VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
        PhysicsWorld *physics = physics_world_create(obj_world, NULL);
        physics->gravity = vec3_zero();
        int32_t obj_idx = fixture_add_box(obj_world, vec3_create(-50.0f, 20.0f, 0.0f),
                                                     vec3_create(0.1f, 0.1f, 0.1f), MAT_STONE);
        int32_t body_idx = physics_world_add_body(physics, obj_idx);
        ASSERT(body_idx >= 0);
        physics_body_set_velocity(physics, body_idx, vec3_create(speeds[k], 0.0f, 0.0f));
        for (int32_t tick = 0; tick < 6; tick++)
            physics_world_step(physics, 1.0f / 60.0f);
        ratio[k] = physics_world_get_body(physics, body_idx)->velocity.x / speeds[k];
        physics_world_destroy(physics);
        voxel_object_world_destroy(obj_world);
    }

    printf("(slow=%.4f, fast=%.4f, expected=%.4f) ", ratio[0], ratio[1], powf(PHYS_LINEAR_DAMPING, 6.0f));
    ASSERT_NEAR(ratio[0], powf(PHYS_LINEAR_DAMPING, 6.0f), 1e-3f);
    ASSERT_NEAR(ratio[1], ratio[0], 1e-3f);
    return 1;
}

TEST(physics_static_body_ignores_terrain)
{
    /* Static bodies skipped integration but still went through the terrain solver,
     * which pushed them out of the ground. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 0.5f, 0.0f),
                                                 vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    physics_world_get_body(physics, body_idx)->flags |= PHYS_FLAG_STATIC;
    Vec3 start = obj_world->objects[obj_idx].position;

    for (int32_t tick = 0; tick < 30; tick++)
        physics_world_step(physics, 1.0f / 60.0f);

    float moved = vec3_length(vec3_sub(obj_world->objects[obj_idx].position, start));
    printf("(moved=%.3f) ", moved);
    ASSERT(moved < 1e-6f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_terrain_impact_bounces)
{
    /* The Baumgarte term was subtracted from the bounce impulse, so the deeper a
     * falling body had sunk in during the step, the weaker its bounce, down to none:
     * it kept moving into the ground (v_y -1.55 at 0.15 m). The response must not
     * depend on the depth. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float v_after[2];
    float depths[2] = {0.02f, 0.15f};
    for (int32_t k = 0; k < 2; k++)
    {
        VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
        VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
        PhysicsWorld *physics = physics_world_create(obj_world, terrain);
        physics->gravity = vec3_zero();

        int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 0.8f - depths[k], 0.0f),
                                                     vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
        int32_t body_idx = physics_world_add_body(physics, obj_idx);
        physics_body_set_velocity(physics, body_idx, vec3_create(0.0f, -3.0f, 0.0f));
        physics_world_step(physics, 1.0f / 60.0f);
        v_after[k] = physics_world_get_body(physics, body_idx)->velocity.y;

        physics_world_destroy(physics);
        voxel_object_world_destroy(obj_world);
        volume_destroy(terrain);
    }

    printf("(v_y shallow=%.3f, deep=%.3f) ", v_after[0], v_after[1]);
    ASSERT(v_after[0] > 0.0f);
    ASSERT_NEAR(v_after[1], v_after[0], 0.05f);
    return 1;
}

TEST(physics_terrain_friction_no_energy_gain)
{
    /* Friction used the normal's effective mass. At the foot of an upright rod the
     * tangent's is several times larger, so the friction impulse overshot: a rod
     * sliding on the ground (no normal velocity, friction only) went from 20 to 92
     * units of kinetic energy in one step. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);
    physics->gravity = vec3_zero();

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 0.5f + 0.8f - 0.1f, 0.0f),
                                                 vec3_create(0.05f, 0.8f, 0.05f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    RigidBody *body = physics_world_get_body(physics, body_idx);
    VoxelObject *obj = &obj_world->objects[obj_idx];
    physics_body_set_velocity(physics, body_idx, vec3_create(0.5f, 0.0f, 0.0f));

    float before = kinetic_energy(body, obj);
    physics_world_step(physics, 1.0f / 60.0f);
    float after = kinetic_energy(body, obj);

    printf("(energy %.3f -> %.3f) ", before, after);
    ASSERT(after <= before);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_resting_awake_body_falls_when_ground_removed)
{
    /* A body at rest on terrain skips its ground check, and only sleeping bodies
     * were woken, so an awake body at rest hovered once the ground under it went.
     * The volume's removal log now ungrounds it on the next step. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 1.5f, 0.0f),
                                                 vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    RigidBody *body = physics_world_get_body(physics, body_idx);

    bool resting_awake = false;
    for (int32_t tick = 0; tick < 600 && !resting_awake; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        resting_awake = (body->flags & PHYS_FLAG_GROUNDED) && !(body->flags & PHYS_FLAG_SLEEPING) &&
                        body->ground_frames >= PHYS_GROUND_PERSIST_FRAMES && body->sleep_frames > 3 &&
                        vec3_length(body->velocity) == 0.0f;
    }
    ASSERT(resting_awake);

    float rest_y = obj_world->objects[obj_idx].position.y;
    fixture_fill_box(terrain, vec3_create(-2.0f, 0.0f, -2.0f), vec3_create(2.0f, 0.6f, 2.0f), 0);
    volume_rebuild_all_occupancy(terrain);
    for (int32_t tick = 0; tick < 30; tick++)
        physics_world_step(physics, 1.0f / 60.0f);

    float drop = rest_y - obj_world->objects[obj_idx].position.y;
    printf("(drop=%.3f) ", drop);
    ASSERT(drop > 0.2f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(physics_touching_neighbours_sleep)
{
    /* Every contact woke both bodies, so two boxes resting on the ground side by side
     * kept each other awake forever. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t obj_a = fixture_add_box(obj_world, vec3_create(-0.29f, 0.81f, 0.0f),
                                               vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    int32_t obj_b = fixture_add_box(obj_world, vec3_create(0.29f, 0.81f, 0.0f),
                                               vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    RigidBody *body_a = physics_world_get_body(physics, physics_world_add_body(physics, obj_a));
    RigidBody *body_b = physics_world_get_body(physics, physics_world_add_body(physics, obj_b));
    ASSERT(body_a && body_b);

    int32_t touching_ticks = 0;
    for (int32_t tick = 0; tick < 600; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        ObjectCollisionPair pairs[4];
        touching_ticks += physics_detect_object_pairs(physics, pairs, 4) > 0 ? 1 : 0;
    }

    float gap = (obj_world->objects[obj_b].position.x - obj_world->objects[obj_a].position.x) -
                (obj_world->objects[obj_a].shape_half_extents.x + obj_world->objects[obj_b].shape_half_extents.x);
    printf("(sleeping=%d,%d, gap=%.4f, touching_ticks=%d) ", (body_a->flags & PHYS_FLAG_SLEEPING) != 0,
           (body_b->flags & PHYS_FLAG_SLEEPING) != 0, gap, touching_ticks);
    ASSERT(gap < 0.0f);
    ASSERT((body_a->flags & PHYS_FLAG_SLEEPING) && (body_b->flags & PHYS_FLAG_SLEEPING));

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(split_spinning_object_keeps_rigid_motion)
{
    /* Pieces of a spinning object must move with the rigid motion it had: v + w x
     * (piece COM - original COM). The parent kept its old COM velocity and the new
     * piece was offset from the parent's grid centre, which made up momentum. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = fixture_add_box(obj_world, vec3_create(0.0f, 20.0f, 0.0f),
                                                 vec3_create(1.5f, 0.25f, 0.25f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);
    Vec3 v0 = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 w0 = vec3_create(0.0f, 3.0f, 0.0f);
    physics_body_set_velocity(physics, body_idx, v0);
    physics_body_set_angular_velocity(physics, body_idx, w0);

    VoxelObject *obj = &obj_world->objects[obj_idx];
    Vec3 com0 = physics_object_com(obj);
    int32_t min_x, max_x;
    voxel_x_range(obj, &min_x, &max_x);
    cut_slice_x(obj, min_x + 3); /* a short piece and a long one */
    voxel_object_world_queue_split(obj_world, obj_idx);
    voxel_object_world_process_splits(obj_world);
    physics_world_sync_objects(physics);

    int32_t pieces = 0;
    float worst = 0.0f;
    for (int32_t i = 0; i < obj_world->object_count; i++)
    {
        if (!obj_world->objects[i].active)
            continue;
        RigidBody *rb = physics_world_get_body(physics, physics_world_find_body_for_object(physics, i));
        ASSERT(rb != NULL);
        Vec3 expected = vec3_add(v0, vec3_cross(w0, vec3_sub(physics_object_com(&obj_world->objects[i]), com0)));
        worst = maxf(worst, vec3_length(vec3_sub(rb->velocity, expected)));
        ASSERT(vec3_length(vec3_sub(rb->angular_velocity, w0)) < 1e-4f);
        pieces++;
    }

    printf("(pieces=%d, worst_v_err=%.4f) ", pieces, worst);
    ASSERT(pieces == 2);
    ASSERT(worst < 1e-3f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(split_piece_in_reused_slot_gets_parent_motion)
{
    /* An object freed and its slot taken by a split piece before the next sync: the
     * piece found the old occupant's body in the slot and kept its velocity. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t doomed = fixture_add_box(obj_world, vec3_create(-8.0f, 20.0f, 0.0f),
                                                vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t bar = fixture_add_box(obj_world, vec3_create(0.0f, 20.0f, 0.0f),
                                             vec3_create(1.0f, 0.25f, 0.25f), MAT_STONE);
    physics_world_sync_objects(physics);
    Vec3 bar_velocity = vec3_create(0.0f, 0.0f, 2.0f);
    physics_body_set_velocity(physics, physics_world_find_body_for_object(physics, doomed),
                              vec3_create(25.0f, 0.0f, 0.0f));
    physics_body_set_velocity(physics, physics_world_find_body_for_object(physics, bar), bar_velocity);

    voxel_object_world_free_slot(obj_world, doomed);
    VoxelObject *bar_obj = &obj_world->objects[bar];
    int32_t min_x, max_x;
    voxel_x_range(bar_obj, &min_x, &max_x);
    cut_slice_x(bar_obj, min_x + 4);
    voxel_object_world_queue_split(obj_world, bar);
    voxel_object_world_process_splits(obj_world);
    ASSERT(obj_world->objects[doomed].active); /* the piece took the freed slot */
    physics_world_sync_objects(physics);

    RigidBody *piece = physics_world_get_body(physics, physics_world_find_body_for_object(physics, doomed));
    ASSERT(piece != NULL);
    float err = vec3_length(vec3_sub(piece->velocity, bar_velocity));
    printf("(piece v=(%.2f,%.2f,%.2f)) ", piece->velocity.x, piece->velocity.y, piece->velocity.z);
    ASSERT(err < 1e-3f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(new_object_in_reused_slot_starts_at_rest)
{
    /* An object freed and its slot taken by an unrelated new object before the next sync:
     * the newcomer inherited the old occupant's body and velocity */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t doomed = fixture_add_box(obj_world, vec3_create(-8.0f, 20.0f, 0.0f),
                                                vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    physics_world_sync_objects(physics);
    physics_body_set_velocity(physics, physics_world_find_body_for_object(physics, doomed),
                              vec3_create(25.0f, 0.0f, 0.0f));

    voxel_object_world_free_slot(obj_world, doomed);
    int32_t newcomer = fixture_add_box(obj_world, vec3_create(4.0f, 20.0f, 0.0f),
                                                  vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT_EQ(newcomer, doomed);
    physics_world_sync_objects(physics);

    RigidBody *body = physics_world_get_body(physics, physics_world_find_body_for_object(physics, newcomer));
    ASSERT(body != NULL);
    printf("(v=(%.2f,%.2f,%.2f)) ", body->velocity.x, body->velocity.y, body->velocity.z);
    ASSERT(vec3_length(body->velocity) < 1e-6f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_impact_wakes_sleeping_body)
{
    /* Contacts leave a sleeping body alone unless something runs into it */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t target = fixture_add_box(obj_world, vec3_create(0.0f, 0.81f, 0.0f),
                                                vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    RigidBody *body = physics_world_get_body(physics, physics_world_add_body(physics, target));
    ASSERT(body != NULL);
    for (int32_t tick = 0; tick < 300 && !(body->flags & PHYS_FLAG_SLEEPING); tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    ASSERT(body->flags & PHYS_FLAG_SLEEPING);
    Vec3 rest = obj_world->objects[target].position;

    int32_t thrown = fixture_add_box(obj_world, vec3_create(-1.5f, 1.2f, 0.0f),
                                                vec3_create(0.2f, 0.2f, 0.2f), MAT_STONE);
    physics_body_set_velocity(physics, physics_world_add_body(physics, thrown), vec3_create(8.0f, 0.0f, 0.0f));

    bool woke = false;
    for (int32_t tick = 0; tick < 30; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        woke = woke || !(body->flags & PHYS_FLAG_SLEEPING);
    }

    float moved = vec3_length(vec3_sub(obj_world->objects[target].position, rest));
    printf("(woke=%d, moved=%.3f) ", woke ? 1 : 0, moved);
    ASSERT(woke);
    ASSERT(moved > 0.02f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(object_friction_does_not_reverse_sliding)
{
    /* Object contacts used the normal's effective mass for friction too. A rod landing
     * on a block while sliding at 0.3 m/s left the contact sliding back at 0.88 m/s;
     * friction can only bring the sliding to a stop. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);
    physics->gravity = vec3_zero();

    int32_t base = fixture_add_box(obj_world, vec3_create(0.0f, 5.0f, 0.0f),
                                              vec3_create(1.0f, 0.5f, 1.0f), MAT_STONE);
    int32_t rod = fixture_add_box(obj_world, vec3_create(0.0f, 5.5f + 0.8f - 0.03f, 0.0f),
                                             vec3_create(0.05f, 0.8f, 0.05f), MAT_STONE);
    physics_world_get_body(physics, physics_world_add_body(physics, base))->flags |= PHYS_FLAG_STATIC;
    int32_t body_idx = physics_world_add_body(physics, rod);
    RigidBody *body = physics_world_get_body(physics, body_idx);
    VoxelObject *obj = &obj_world->objects[rod];
    physics_body_set_velocity(physics, body_idx, vec3_create(0.3f, -1.0f, 0.0f));

    float before = kinetic_energy(body, obj);
    physics_world_step(physics, 1.0f / 60.0f);
    float after = kinetic_energy(body, obj);

    ObjectCollisionPair pairs[4];
    ASSERT(physics_detect_object_pairs(physics, pairs, 4) == 1);
    Vec3 r = vec3_sub(pairs[0].contact_point, physics_object_com(obj));
    float sliding = rigid_body_point_velocity(body, r).x;

    printf("(contact sliding 0.300 -> %.3f, energy %.3f -> %.3f) ", sliding, before, after);
    ASSERT(sliding > -0.02f);
    ASSERT(after <= before);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

static int32_t create_sphere_object(VoxelObjectWorld *world, Vec3 position, float radius)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));
    obj->position = position;
    obj->orientation = quat_identity();
    obj->voxel_size = world->voxel_size;
    obj->active = true;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float r = radius / world->voxel_size;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = (float)x + 0.5f - half_grid;
                float dy = (float)y + 0.5f - half_grid;
                float dz = (float)z + 0.5f - half_grid;
                if (dx * dx + dy * dy + dz * dz <= r * r)
                {
                    obj->voxels[vobj_index(x, y, z)].material = MAT_STONE;
                    obj->voxel_count++;
                }
            }

    voxel_object_recalc_shape(obj);
    return slot;
}

TEST(object_contact_needs_hull_overlap)
{
    /* Hulls closer than their margins but not overlapping fell back to an OBB test,
     * which for spheres approaching diagonally reported the OBB corners' overlap:
     * ~0.7 m of penetration for shapes 0.1 m apart. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    float gaps[2] = {0.0f, 0.19f};
    ObjectCollisionPair pairs[2][4];
    int32_t counts[2];
    for (int32_t k = 0; k < 2; k++)
    {
        VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.125f);
        PhysicsWorld *physics = physics_world_create(obj_world, NULL);
        float d = (2.0f + gaps[k]) / sqrtf(3.0f);
        Vec3 pos_a = vec3_create(0.0f, 10.0f, 0.0f);
        Vec3 pos_b = vec3_create(d, 10.0f + d, d);
        int32_t a = create_sphere_object(obj_world, pos_a, 1.0f);
        int32_t b = create_sphere_object(obj_world, pos_b, 1.0f);
        ASSERT(physics_world_add_body(physics, a) >= 0 && physics_world_add_body(physics, b) >= 0);

        /* One tiny step fills the broadphase; undo whatever its contact did */
        physics->gravity = vec3_zero();
        physics_world_step(physics, 1e-6f);
        obj_world->objects[a].position = pos_a;
        obj_world->objects[b].position = pos_b;
        counts[k] = physics_detect_object_pairs(physics, pairs[k], 4);
        physics_world_destroy(physics);
        voxel_object_world_destroy(obj_world);
    }

    printf("(touching: pairs=%d pen=%.3f, apart: pairs=%d pen=%.3f) ", counts[0],
           counts[0] ? pairs[0][0].penetration : 0.0f, counts[1], counts[1] ? pairs[1][0].penetration : 0.0f);
    ASSERT(counts[0] == 1 && pairs[0][0].penetration < 0.3f);
    ASSERT(counts[1] == 0);
    return 1;
}

TEST(epa_depth_matches_sat_on_boxes)
{
    RngState rng;
    rng_seed(&rng, 0xE9A);
    ConvexHull hull_a, hull_b;
    int32_t tested = 0;
    float worst = 0.0f;

    for (int32_t i = 0; i < 20000; i++)
    {
        Vec3 he_a = vec3_create(rng_range_f32(&rng, 0.05f, 1.0f), rng_range_f32(&rng, 0.05f, 1.0f),
                                rng_range_f32(&rng, 0.05f, 1.0f));
        Vec3 he_b = vec3_create(rng_range_f32(&rng, 0.05f, 1.0f), rng_range_f32(&rng, 0.05f, 1.0f),
                                rng_range_f32(&rng, 0.05f, 1.0f));
        Quat rot_a = random_orientation(&rng);
        Quat rot_b = random_orientation(&rng);
        Vec3 pos_a = vec3_create(rng_range_f32(&rng, -5.0f, 5.0f), rng_range_f32(&rng, -5.0f, 5.0f),
                                 rng_range_f32(&rng, -5.0f, 5.0f));
        Vec3 dir = vec3_create(signed_half(&rng), signed_half(&rng), signed_half(&rng));
        if (vec3_length(dir) < 1e-3f)
            dir = vec3_create(1.0f, 0.0f, 0.0f);
        float reach = vec3_length(he_a) + vec3_length(he_b);
        Vec3 pos_b = vec3_add(pos_a, vec3_scale(vec3_normalize(dir), rng_float(&rng) * reach));

        float depth = obb_sat_depth(pos_a, rot_a, he_a, pos_b, rot_b, he_b);
        if (depth < 1e-3f)
            continue;

        build_box_hull(he_a, &hull_a);
        build_box_hull(he_b, &hull_b);
        GJKSimplex simplex;
        EPAResult epa;
        ASSERT(gjk_intersect(&hull_a, pos_a, rot_a, &hull_b, pos_b, rot_b, &simplex));
        ASSERT(epa_penetration(&hull_a, pos_a, rot_a, &hull_b, pos_b, rot_b, &simplex, &epa));
        worst = maxf(worst, fabsf(epa.depth - depth));
        tested++;
    }

    printf("(pairs=%d, worst_depth_err=%.5f) ", tested, worst);
    ASSERT(tested > 10000);
    ASSERT(worst < 1e-3f);
    return 1;
}

TEST(sap_matches_brute_force)
{
    /* Sorted endpoint list kept between queries: pairs must match an all-pairs AABB
     * test while boxes move, appear and disappear */
    static SAPBroadphase sap;
    static bool expected[SAP_MAX_BODIES][SAP_MAX_BODIES];
    static bool seen[SAP_MAX_BODIES][SAP_MAX_BODIES];
    static SAPPair pairs[SAP_MAX_PAIRS];
    Vec3 mins[SAP_MAX_BODIES], maxs[SAP_MAX_BODIES];
    bool active[SAP_MAX_BODIES] = {false};
    RngState rng;
    rng_seed(&rng, 31337);
    sap_init(&sap);

    int32_t total_pairs = 0;
    for (int32_t frame = 0; frame < 30; frame++)
    {
        for (int32_t b = 0; b < 300; b++)
        {
            /* Mostly small moves (the coherent case), sometimes a jump across the range */
            Vec3 jitter = vec3_create(signed_half(&rng), signed_half(&rng), signed_half(&rng));
            Vec3 c = (frame == 0 || rng_float(&rng) < 0.05f)
                         ? vec3_create(rng_range_f32(&rng, -20.0f, 20.0f), rng_range_f32(&rng, -5.0f, 5.0f),
                                       rng_range_f32(&rng, -5.0f, 5.0f))
                         : vec3_add(vec3_scale(vec3_add(mins[b], maxs[b]), 0.5f), jitter);
            Vec3 he = vec3_create(rng_range_f32(&rng, 0.2f, 1.5f), rng_range_f32(&rng, 0.2f, 1.5f),
                                  rng_range_f32(&rng, 0.2f, 1.5f));
            mins[b] = vec3_sub(c, he);
            maxs[b] = vec3_add(c, he);

            if (frame == 0 || rng_float(&rng) < 0.1f)
                active[b] = rng_float(&rng) < 0.8f;
            if (active[b])
                sap_update_body(&sap, b, mins[b], maxs[b]);
            else
                sap_remove_body(&sap, b);
        }

        int32_t expected_count = 0;
        for (int32_t a = 0; a < 300; a++)
            for (int32_t b = a + 1; b < 300; b++)
            {
                expected[a][b] = active[a] && active[b] && mins[a].x <= maxs[b].x && mins[b].x <= maxs[a].x &&
                                 mins[a].y <= maxs[b].y && mins[b].y <= maxs[a].y && mins[a].z <= maxs[b].z &&
                                 mins[b].z <= maxs[a].z;
                seen[a][b] = false;
                expected_count += expected[a][b] ? 1 : 0;
            }

        int32_t count = sap_query_pairs(&sap, pairs, SAP_MAX_PAIRS);
        ASSERT(count == expected_count);
        for (int32_t p = 0; p < count; p++)
        {
            int32_t a = pairs[p].body_a, b = pairs[p].body_b;
            ASSERT(a < b && expected[a][b] && !seen[a][b]);
            seen[a][b] = true;
        }
        total_pairs += count;
    }

    printf("(pairs over 30 frames=%d) ", total_pairs);
    ASSERT(total_pairs > 1000);
    return 1;
}

TEST(hull_of_random_points)
{
    /* Points on a sphere are all hull vertices; points inside never are. The hull's
     * support must match the brute-force maximum in every direction. */
    static Vec3 points[400];
    RngState rng;
    rng_seed(&rng, 2024);
    for (int32_t i = 0; i < 400; i++)
    {
        Vec3 d = vec3_create(signed_half(&rng), signed_half(&rng), signed_half(&rng));
        if (vec3_length(d) < 1e-3f)
            d = vec3_create(0.0f, 0.0f, 1.0f);
        points[i] = vec3_scale(vec3_normalize(d), i < 100 ? 2.0f : rng_range_f32(&rng, 0.0f, 1.5f));
    }

    static ConvexHull hull;
    convex_hull_build(points, 400, &hull);

    float worst = 0.0f;
    for (int32_t k = 0; k < 500; k++)
    {
        Vec3 dir = vec3_create(signed_half(&rng), signed_half(&rng), signed_half(&rng));
        float best = -1e30f;
        for (int32_t i = 0; i < 400; i++)
            best = maxf(best, vec3_dot(points[i], dir));
        worst = maxf(worst, best - vec3_dot(hull.vertices[convex_hull_support(&hull, dir)], dir));
    }

    printf("(vertices=%d, worst_support_err=%.6f) ", hull.vertex_count, worst);
    ASSERT(hull.vertex_count == 100);
    ASSERT(worst < 1e-5f);
    return 1;
}

/* Stone sphere of radius r voxels (a (2r+1)^3 grid) with its grid's minimum corner at origin */
static int32_t add_voxel_sphere(VoxelObjectWorld *world, int32_t r, Vec3 origin)
{
    static uint8_t voxels[VOBJ_TOTAL_VOXELS];
    int32_t n = 2 * r + 1;
    for (int32_t z = 0; z < n; z++)
        for (int32_t y = 0; y < n; y++)
            for (int32_t x = 0; x < n; x++)
            {
                int32_t dx = x - r, dy = y - r, dz = z - r;
                voxels[x + y * n + z * n * n] = (dx * dx + dy * dy + dz * dz <= r * r) ? MAT_STONE : 0;
            }
    return voxel_object_world_add_from_voxels(world, voxels, n, n, n, origin, world->voxel_size);
}

TEST(large_objects_rest_stable_and_sleep)
{
    /* Bodies at rest sleep once their centre of mass is over their ground support. The
     * support came from a box decomposition of the voxels capped at 48 boxes, which
     * covered only part of anything bigger than a radius-5 sphere: its centroid sat off
     * the centre of mass and those bodies never slept. */
    Bounds3D bounds = {-6.4f, 6.4f, 0.0f, 12.8f, -6.4f, 6.4f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 1.0f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    static const int32_t radii[4] = {3, 5, 7, 7};
    int32_t objs[4];
    for (int32_t i = 0; i < 4; i++)
    {
        objs[i] = add_voxel_sphere(obj_world, radii[i], vec3_create(-5.0f + 2.5f * (float)i, 1.3f, -0.75f));
        ASSERT(objs[i] >= 0);
    }
    physics_world_sync_objects(physics);
    for (int32_t tick = 0; tick < 600; tick++)
        physics_world_step(physics, 1.0f / 60.0f);

    for (int32_t i = 0; i < 4; i++)
    {
        const RigidBody *body = &physics->bodies[physics_world_find_body_for_object(physics, objs[i])];
        printf("(r=%d flags=0x%x) ", radii[i], body->flags);
        ASSERT(body->flags & PHYS_FLAG_SLEEPING);
    }

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(body_on_removed_terrain_island_falls)
{
    /* A box asleep on a shelf held by one pillar. Cutting the pillar woke only what was
     * near the cut; the shelf then went as a detached island, away from the cut, and
     * nothing woke the box: it slept on in mid-air where the shelf had been. */
    VoxelVolume *vol = volume_create_dims(4, 2, 4, vec3_zero(), 0.1f);
    fixture_fill_box(vol, vec3_create(6.0f, 0.0f, 6.0f), vec3_create(6.2f, 3.0f, 6.2f), MAT_STONE);
    fixture_fill_box(vol, vec3_create(4.0f, 3.0f, 4.0f), vec3_create(8.0f, 3.2f, 8.0f), MAT_STONE);

    Bounds3D bounds = {0.0f, 12.8f, 0.0f, 6.4f, 0.0f, 12.8f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    int32_t box = fixture_add_box(obj_world, vec3_create(4.6f, 3.6f, 4.6f), vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    PhysicsWorld *physics = physics_world_create(obj_world, vol);
    physics_world_sync_objects(physics);
    for (int32_t tick = 0; tick < 240; tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    RigidBody *body = &physics->bodies[physics_world_find_body_for_object(physics, box)];
    ASSERT(body->flags & PHYS_FLAG_SLEEPING);
    float rest_y = obj_world->objects[box].position.y;

    /* Cut the pillar's top two voxel layers (voxels 60-61 in x and z, 28-29 in y) */
    volume_edit_begin(vol);
    for (int32_t y = 28; y <= 29; y++)
        for (int32_t z = 60; z <= 61; z++)
            for (int32_t x = 60; x <= 61; x++)
                fixture_edit_voxel(vol, x, y, z, MATERIAL_EMPTY);
    volume_edit_end(vol);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));
    ConnectivityResult result;
    volume_consume_pending_analysis(vol);
    connectivity_analyze_dirty(vol, vol->bounds.min_y + 0.1f, &work, &result);
    ASSERT(result.island_count == 1);
    connectivity_remove_island(vol, &result.islands[0], &work);

    for (int32_t tick = 0; tick < 120; tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    float y = obj_world->objects[box].position.y;
    printf("(rest y=%.2f, 2 s after the shelf went: y=%.2f) ", rest_y, y);
    ASSERT(y < rest_y - 1.0f);

    connectivity_work_destroy(&work);
    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(vol);
    return 1;
}

TEST(dig_under_resting_body_wakes_it)
{
    /* Terrain emptied under a body at rest wakes it through the volume's removal log,
     * without the editing code waking the region itself */
    Bounds3D bounds = {-6.4f, 6.4f, 0.0f, 12.8f, -6.4f, 6.4f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 1.0f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    int32_t box = fixture_add_box(obj_world, vec3_create(0.0f, 1.5f, 0.0f), vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);
    physics_world_sync_objects(physics);
    for (int32_t tick = 0; tick < 240; tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    RigidBody *body = &physics->bodies[physics_world_find_body_for_object(physics, box)];
    ASSERT(body->flags & PHYS_FLAG_SLEEPING);
    float rest_y = obj_world->objects[box].position.y;

    /* A pit 1.2 m wide and 0.5 m deep under the box, as one edit batch */
    volume_edit_begin(terrain);
    for (int32_t y = 5; y < 10; y++)
        for (int32_t z = 58; z < 70; z++)
            for (int32_t x = 58; x < 70; x++)
                fixture_edit_voxel(terrain, x, y, z, MATERIAL_EMPTY);
    volume_edit_end(terrain);

    for (int32_t tick = 0; tick < 120; tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    float y = obj_world->objects[box].position.y;
    printf("(rest y=%.2f, in the pit: y=%.2f) ", rest_y, y);
    ASSERT(y < rest_y - 0.4f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
    return 1;
}

TEST(particles_settled_debris_expires)
{
    /* Settled debris used to stay until the ring wrapped over it, so after enough
     * destruction every tick walked 65536 particles. It rests PARTICLE_SETTLED_LIFETIME
     * seconds now; moving particles are not aged out. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    ParticleSystem *sys = particle_system_create(bounds);
    ASSERT(sys != NULL);

    for (int32_t i = 0; i < 100; i++)
        particle_system_add(sys, vec3_create(-2.0f + 0.04f * (float)i, 1.0f, 0.3f), vec3_zero(),
                            vec3_create(1.0f, 1.0f, 1.0f), 0.04f);
    int32_t tick = 0;
    for (; tick < 120; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);
    ASSERT(sys->count == 100 && sys->awake_count == 0);

    /* Still there just before the lifetime is up, gone once a check cycle passed it */
    int32_t lifetime_ticks = (int32_t)(PARTICLE_SETTLED_LIFETIME * 60.0f);
    for (; tick < lifetime_ticks; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);
    ASSERT(sys->count == 100);
    for (; tick < lifetime_ticks + 120; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);
    printf("(after %.0f s: %d left) ", (float)tick / 60.0f, sys->count);
    ASSERT(sys->count == 0);

    particle_system_destroy(sys);
    volume_destroy(terrain);
    return 1;
}

TEST(particles_full_system_replaces_settled_first)
{
    /* At capacity a new particle takes a settled particle's place, so debris in flight
     * is not cut off while old debris lies around */
    Bounds3D bounds = {-12.8f, 12.8f, 0.0f, 8.0f, -12.8f, 12.8f};
    VoxelVolume *terrain = volume_create_dims(8, 2, 8, vec3_create(-12.8f, 0.0f, -12.8f), 0.1f);
    fixture_fill_box(terrain, vec3_create(-12.8f, 0.0f, -12.8f), vec3_create(12.8f, 1.0f, 12.8f), MAT_STONE);
    ParticleSystem *sys = particle_system_create(bounds);
    ASSERT(sys != NULL);

    for (int32_t i = 0; i < PARTICLE_MAX_COUNT; i++)
        particle_system_add(sys, vec3_create(-12.0f + (float)(i % 256) * 0.094f, 1.04f, -12.0f + (float)(i / 256) * 0.094f),
                            vec3_zero(), vec3_create(1.0f, 1.0f, 1.0f), 0.03f);
    for (int32_t tick = 0; tick < 10; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);
    ASSERT(sys->count == PARTICLE_MAX_COUNT && sys->awake_count == 0);

    for (int32_t i = 0; i < 10; i++)
        particle_system_add(sys, vec3_create(0.0f, 4.0f, 0.0f), vec3_create(1.0f, 0.0f, 0.0f), vec3_create(1.0f, 0.0f, 0.0f), 0.03f);
    ASSERT(sys->count == PARTICLE_MAX_COUNT && sys->awake_count == 10);
    for (int32_t i = 0; i < sys->awake_count; i++)
        ASSERT(!sys->particles[i].settled && sys->particles[i].position.y == 4.0f);
    for (int32_t i = sys->awake_count; i < sys->count; i++)
        ASSERT(sys->particles[i].active && sys->particles[i].settled);

    particle_system_destroy(sys);
    volume_destroy(terrain);
    return 1;
}

TEST(particles_settle_and_wake)
{
    /* Particles dropped on the floor settle; once the floor is gone they fall again */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    ParticleSystem *sys = particle_system_create(bounds);
    ASSERT(sys != NULL);
    RngState rng;
    rng_seed(&rng, 5);

    for (int32_t i = 0; i < 50; i++)
    {
        Vec3 pos = vec3_create(rng_range_f32(&rng, -1.0f, 1.0f), 2.0f, rng_range_f32(&rng, -1.0f, 1.0f));
        particle_system_add(sys, pos, vec3_zero(), vec3_create(1.0f, 1.0f, 1.0f), 0.04f);
    }
    for (int32_t tick = 0; tick < 300; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);

    int32_t settled = 0;
    for (int32_t i = 0; i < sys->count; i++)
    {
        const Particle *p = &sys->particles[i];
        settled += (p->active && p->settled && p->position.y > 0.45f && p->position.y < 0.7f) ? 1 : 0;
    }

    fixture_fill_box(terrain, vec3_create(-5.0f, 0.0f, -5.0f), vec3_create(5.0f, 0.6f, 5.0f), 0);
    for (int32_t tick = 0; tick < 10; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);

    int32_t falling = 0;
    for (int32_t i = 0; i < sys->count; i++)
    {
        const Particle *p = &sys->particles[i];
        falling += (p->active && !p->settled && p->position.y < 0.45f) ? 1 : 0;
    }

    printf("(settled=%d/50, falling after floor removed=%d/50) ", settled, falling);
    ASSERT(settled == 50);
    ASSERT(falling == 50);

    /* Below the world nothing can stop them: they are retired, not left falling forever */
    for (int32_t tick = 0; tick < 60; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);
    ASSERT(sys->count == 0);

    particle_system_destroy(sys);
    volume_destroy(terrain);
    return 1;
}

TEST(particles_pinned_debris_settles)
{
    /* Debris pinned where it cannot move, in a one-voxel slot under an overhang or wedged
     * against a step, had every step undone by the terrain while its velocity stayed above
     * the settle speed, so it never settled. In the game over half the debris ended up like
     * that, stepped every tick for good: 3.5 ms per frame at 20k debris, and growing. */
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    fixture_fill_box(terrain, vec3_create(-2.0f, 0.6f, -2.0f), vec3_create(0.0f, 0.8f, 2.0f), MAT_STONE);
    fixture_fill_box(terrain, vec3_create(1.0f, 0.5f, -2.0f), vec3_create(2.0f, 1.0f, 2.0f), MAT_STONE);
    volume_rebuild_all_occupancy(terrain);
    ParticleSystem *sys = particle_system_create(bounds);
    ASSERT(sys != NULL);

    for (int32_t i = 0; i < 20; i++)
    {
        const float z = -1.9f + 0.19f * (float)i;
        particle_system_add(sys, vec3_create(-1.0f, 0.55f, z), vec3_create(0.0f, -1.0f, 0.0f), vec3_create(1.0f, 1.0f, 1.0f), 0.04f);
        particle_system_add(sys, vec3_create(0.97f, 0.55f, z), vec3_create(0.7f, -0.7f, 0.0f), vec3_create(1.0f, 1.0f, 1.0f), 0.04f);
    }
    for (int32_t tick = 0; tick < 180; tick++)
        particle_system_update(sys, 1.0f / 60.0f, terrain, NULL);

    int32_t moved_off = 0;
    for (int32_t i = 0; i < sys->count; i++)
        moved_off += fabsf(sys->particles[i].position.y - 0.55f) > 0.1f ? 1 : 0;
    printf("(%d of %d still moving after 3 s, %d left their spot) ", sys->awake_count, sys->count, moved_off);
    ASSERT(sys->count == 40);
    ASSERT(sys->awake_count == 0);

    particle_system_destroy(sys);
    volume_destroy(terrain);
    return 1;
}

/* Drop a box of half extent top_he onto a 0.6 m box asleep on the floor; reports the
 * bottom box's deepest sink below its rest height and both final heights */
static void drop_box_on_resting_box(float top_he, float *out_sink, float *out_bottom_y, float *out_top_y,
                                    bool *out_bottom_asleep)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    VoxelVolume *terrain = create_floor_terrain(bounds, 0.1f, 0.5f);
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.1f);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t bottom = fixture_add_box(obj_world, vec3_create(0.0f, 0.81f, 0.0f),
                                                vec3_create(0.3f, 0.3f, 0.3f), MAT_STONE);
    RigidBody *bottom_body = physics_world_get_body(physics, physics_world_add_body(physics, bottom));
    for (int32_t tick = 0; tick < 600 && !(bottom_body->flags & PHYS_FLAG_SLEEPING); tick++)
        physics_world_step(physics, 1.0f / 60.0f);
    float rest_y = obj_world->objects[bottom].position.y;

    int32_t top = fixture_add_box(obj_world, vec3_create(0.0f, rest_y + 0.3f + top_he + 0.035f, 0.0f),
                                             vec3_create(top_he, top_he, top_he), MAT_STONE);
    physics_world_add_body(physics, top);
    float lowest = rest_y;
    for (int32_t tick = 0; tick < 600; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
        lowest = minf(lowest, obj_world->objects[bottom].position.y);
    }

    *out_sink = rest_y - lowest;
    *out_bottom_y = obj_world->objects[bottom].position.y;
    *out_top_y = obj_world->objects[top].position.y;
    *out_bottom_asleep = (bottom_body->flags & PHYS_FLAG_SLEEPING) != 0;

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
}

TEST(stacked_box_does_not_sink_resting_box)
{
    /* The object contact shared its push-out by mass, unaware of the terrain under the
     * bottom box: a 0.6 m box landing on it pushed it 0.28 m into the floor (the
     * terrain solver skips bodies at rest), and under a 1 m or 1.6 m box it sank
     * until it fell out of the world. The ground holds it now, so the top box takes
     * the whole response, and the bottom one stays asleep. */
    const float top_he[3] = {0.3f, 0.5f, 0.8f};
    for (int32_t k = 0; k < 3; k++)
    {
        float sink, bottom_y, top_y;
        bool bottom_asleep;
        drop_box_on_resting_box(top_he[k], &sink, &bottom_y, &top_y, &bottom_asleep);
        printf("(%.1f m box: sink=%.3f, bottom y=%.3f, top y=%.3f, asleep=%d) ", top_he[k] * 2.0f, sink,
               bottom_y, top_y, bottom_asleep ? 1 : 0);
        ASSERT(sink < 0.1f);                                /* within a voxel of its rest */
        ASSERT(bottom_y > 0.8f - 0.1f);                     /* on the floor (top at 0.5) */
        ASSERT(fabsf(top_y - (bottom_y + 0.3f + top_he[k])) < 0.1f); /* still on top */
        ASSERT(bottom_asleep);
    }
    return 1;
}

TEST(odd_sized_objects_rest_on_terrain)
{
    /* Recentring leaves an odd extent half a voxel off the grid centre, but the terrain
     * sample box was centred on the grid centre: objects of odd height rested half a
     * voxel too low, sunk 3 cm into the floor. */
    Bounds3D bounds = {0.0f, 6.4f, 0.0f, 6.4f, 0.0f, 6.4f};
    float worst_low = 0.0f, worst_high = 0.0f;
    for (int32_t height = 2; height <= 5; height++)
    {
        VoxelVolume *terrain = fixture_volume_create(2, 2, 2, bounds);
        float vs = terrain->voxel_size;
        fixture_fill_box(terrain, vec3_zero(), vec3_create(6.4f, 1.0f, 6.4f), MAT_STONE);
        VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, vs);
        uint8_t voxels[4 * 5 * 4];
        memset(voxels, MAT_STONE, sizeof(voxels));
        int32_t idx = voxel_object_world_add_from_voxels(obj_world, voxels, 4, height, 4,
                                                         vec3_create(3.0f, 2.0f, 3.0f), vs);
        ASSERT(idx >= 0);
        PhysicsWorld *physics = physics_world_create(obj_world, terrain);
        physics_world_sync_objects(physics);
        for (int32_t tick = 0; tick < 240; tick++)
            physics_world_step(physics, 1.0f / 60.0f);

        /* Lowest voxel corner in world space */
        const VoxelObject *obj = &obj_world->objects[idx];
        float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
        float lowest = 1e30f;
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            if (obj->voxels[i].material == 0)
                continue;
            int32_t x, y, z;
            vobj_coords(i, &x, &y, &z);
            for (int32_t c = 0; c < 8; c++)
            {
                Vec3 local = vec3_create(((float)(x + (c & 1)) - half_grid) * vs,
                                         ((float)(y + ((c >> 1) & 1)) - half_grid) * vs,
                                         ((float)(z + ((c >> 2) & 1)) - half_grid) * vs);
                lowest = minf(lowest, vec3_add(obj->position, quat_rotate_vec3(obj->orientation, local)).y);
            }
        }
        printf("(h%d: %.3f) ", height, lowest - 1.0f);
        worst_low = minf(worst_low, lowest - 1.0f);
        worst_high = maxf(worst_high, lowest - 1.0f);

        physics_world_destroy(physics);
        voxel_object_world_destroy(obj_world);
        volume_destroy(terrain);
    }

    ASSERT(worst_low > -0.01f);
    ASSERT(worst_high < 0.05f);
    return 1;
}

/* Object of n^3 voxels starting at grid cell 14: odd n leaves the box half a voxel off
 * the grid centre */
static int32_t create_cube_at_cell_14(VoxelObjectWorld *world, Vec3 position, Quat orientation, int32_t n)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;
    VoxelObject *obj = &world->objects[slot];
    obj->position = position;
    obj->orientation = quat_identity();
    obj->voxel_size = world->voxel_size;
    obj->active = true;
    for (int32_t z = 0; z < n; z++)
        for (int32_t y = 0; y < n; y++)
            for (int32_t x = 0; x < n; x++)
            {
                obj->voxels[vobj_index(14 + x, 14 + y, 14 + z)].material = MAT_STONE;
                obj->voxel_count++;
            }
    voxel_object_recalc_shape(obj);
    obj->orientation = orientation;
    return slot;
}

/* Rotation taking unit vector `from` onto unit vector `to` (not opposite) */
static Quat rotation_between(Vec3 from, Vec3 to)
{
    Vec3 axis = vec3_normalize(vec3_cross(from, to));
    return axis_angle(axis, acosf(vec3_dot(from, to)));
}

TEST(rotated_odd_objects_reach_narrow_phase)
{
    /* The broadphase box was centred on the grid centre with half a voxel of padding.
     * Rotated, the off-centre box of an odd-sized object reaches up to 0.87 voxel past
     * that: two 3x3x3 cubes turned corner to corner overlapped (GJK says so) but never
     * became a broadphase pair, and passed through each other. */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);
    physics->gravity = vec3_zero();

    Vec3 diagonal = vec3_normalize(vec3_create(1.0f, 1.0f, 1.0f));
    Vec3 pos_a = vec3_create(1.65f, 10.0f, 0.0f);
    Vec3 pos_b = vec3_create(0.0f, 10.0f, 0.0f);
    int32_t a = create_cube_at_cell_14(obj_world, pos_a, rotation_between(diagonal, vec3_create(1.0f, 0.0f, 0.0f)), 3);
    int32_t b = create_cube_at_cell_14(obj_world, pos_b, rotation_between(diagonal, vec3_create(-1.0f, 0.0f, 0.0f)), 3);
    ASSERT(a >= 0 && b >= 0);
    int32_t body_a = physics_world_add_body(physics, a);
    int32_t body_b = physics_world_add_body(physics, b);
    ASSERT(body_a >= 0 && body_b >= 0);
    physics_world_get_body(physics, body_b)->flags |= PHYS_FLAG_STATIC;

    /* One tiny step fills the broadphase; undo whatever its contact did */
    physics_world_step(physics, 1e-6f);
    obj_world->objects[a].position = pos_a;
    obj_world->objects[b].position = pos_b;

    /* Every voxel corner lies inside the body's broadphase box */
    float worst_outside = 0.0f;
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    for (int32_t k = 0; k < 2; k++)
    {
        const VoxelObject *obj = &obj_world->objects[k == 0 ? a : b];
        int32_t body = k == 0 ? body_a : body_b;
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            if (obj->voxels[i].material == 0)
                continue;
            int32_t x, y, z;
            vobj_coords(i, &x, &y, &z);
            for (int32_t c = 0; c < 8; c++)
            {
                Vec3 local = vec3_create(((float)(x + (c & 1)) - half_grid) * obj->voxel_size,
                                         ((float)(y + ((c >> 1) & 1)) - half_grid) * obj->voxel_size,
                                         ((float)(z + ((c >> 2) & 1)) - half_grid) * obj->voxel_size);
                Vec3 p = vec3_add(obj->position, quat_rotate_vec3(obj->orientation, local));
                float pc[3] = {p.x, p.y, p.z};
                for (int32_t axis = 0; axis < 3; axis++)
                {
                    worst_outside = maxf(worst_outside, physics->broadphase->aabb_min[body][axis] - pc[axis]);
                    worst_outside = maxf(worst_outside, pc[axis] - physics->broadphase->aabb_max[body][axis]);
                }
            }
        }
    }

    ObjectCollisionPair pairs[4];
    int32_t pair_count = physics_detect_object_pairs(physics, pairs, 4);

    /* The narrow phase on its own does see the overlap */
    ConvexHull hull_a, hull_b;
    convex_hull_build(obj_world->objects[a].surface_voxels, obj_world->objects[a].surface_voxel_count, &hull_a);
    convex_hull_build(obj_world->objects[b].surface_voxels, obj_world->objects[b].surface_voxel_count, &hull_b);
    GJKSimplex simplex;
    bool overlap = gjk_intersect(&hull_a, pos_a, obj_world->objects[a].orientation, &hull_b, pos_b,
                                 obj_world->objects[b].orientation, &simplex);

    printf("(corner outside aabb=%.4f, gjk overlap=%d, pairs=%d) ", worst_outside, overlap ? 1 : 0, pair_count);
    ASSERT(overlap);
    ASSERT(worst_outside < 1e-4f);
    ASSERT(pair_count == 1);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(reused_slot_does_not_find_previous_body)
{
    /* Between a free and the next sync, a slot's new object was handed the previous
     * occupant's body (and velocity) by find_body_for_object and add_body */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t doomed = fixture_add_box(obj_world, vec3_create(-8.0f, 20.0f, 0.0f),
                                                vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t old_body = physics_world_add_body(physics, doomed);
    ASSERT(old_body >= 0);
    physics_body_set_velocity(physics, old_body, vec3_create(25.0f, 0.0f, 0.0f));

    voxel_object_world_free_slot(obj_world, doomed);
    int32_t newcomer = fixture_add_box(obj_world, vec3_create(4.0f, 20.0f, 0.0f),
                                                  vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT_EQ(newcomer, doomed);

    int32_t found = physics_world_find_body_for_object(physics, newcomer);
    printf("(found %d, old %d) ", found, old_body);
    ASSERT(found < 0);

    int32_t new_body = physics_world_add_body(physics, newcomer);
    ASSERT(new_body >= 0);
    RigidBody *body = physics_world_get_body(physics, new_body);
    ASSERT(body != NULL);
    ASSERT(body->vobj_alloc_id == obj_world->objects[newcomer].alloc_id);
    ASSERT(vec3_length(body->velocity) < 1e-6f);
    ASSERT(physics_world_find_body_for_object(physics, newcomer) == new_body);

    /* The sync keeps the new body and its mapping */
    physics_world_sync_objects(physics);
    ASSERT(physics_world_find_body_for_object(physics, newcomer) == new_body);
    int32_t live_bodies = 0;
    for (int32_t i = 0; i <= physics->max_body_index; i++)
        live_bodies += (physics->bodies[i].flags & PHYS_FLAG_ACTIVE) ? 1 : 0;
    ASSERT(live_bodies == 1);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

int main(void)
{
    platform_time_init();

    printf("=== Matrix Math Tests ===\n");
    RUN_TEST(mat3_transpose_unit);

    printf("\n=== Quaternion/Vector Math Tests ===\n");
    RUN_TEST(quat_rotate_vec3_y_axis);
    RUN_TEST(vec3_clamp_length_under_limit);
    RUN_TEST(vec3_clamp_length_over_limit);

    printf("\n=== Voxel Object Shape Tests ===\n");
    RUN_TEST(shape_half_extents_symmetric);
    RUN_TEST(bounding_sphere_accuracy);

    printf("\n=== Object Fragmentation Tests ===\n");
    RUN_TEST(object_split_creates_fragments);
    RUN_TEST(object_split_dumbbell);
    RUN_TEST(object_slot_no_double_free);
    RUN_TEST(detach_counts_voxels_beyond_output_cap);

    printf("\n=== Object Raycast Tests ===\n");
    RUN_TEST(object_raycast_hit);
    RUN_TEST(object_raycast_miss);
    RUN_TEST(object_bvh_matches_linear_queries);

    printf("\n=== Rigid Body Physics Tests ===\n");
    RUN_TEST(physics_world_create_destroy);
    RUN_TEST(physics_body_add_remove);
    RUN_TEST(physics_body_inertia_symmetric);
    RUN_TEST(physics_impulse_rotated_inertia);
    RUN_TEST(physics_spin_about_center_of_mass);
    RUN_TEST(physics_integration_gravity);
    RUN_TEST(physics_sleep_from_rest);
    RUN_TEST(physics_velocity_clamping);
    RUN_TEST(physics_energy_decreases);
    RUN_TEST(physics_zero_mass_guard);
    RUN_TEST(physics_terrain_contact);
    RUN_TEST(physics_terrain_settling);
    RUN_TEST(bodies_and_debris_pass_water_and_flora);
    RUN_TEST(physics_no_jitter_after_sleep);
    RUN_TEST(physics_performance_128_bodies);
    RUN_TEST(physics_damping_independent_of_substeps);
    RUN_TEST(physics_static_body_ignores_terrain);
    RUN_TEST(physics_terrain_impact_bounces);
    RUN_TEST(physics_terrain_friction_no_energy_gain);
    RUN_TEST(physics_resting_awake_body_falls_when_ground_removed);
    RUN_TEST(physics_touching_neighbours_sleep);
    RUN_TEST(stacked_box_does_not_sink_resting_box);
    RUN_TEST(odd_sized_objects_rest_on_terrain);

    printf("\n=== Physics Flags Tests ===\n");
    RUN_TEST(physics_static_flag);

    printf("\n=== Object-Object Collision Tests ===\n");
    RUN_TEST(object_collision_basic);
    RUN_TEST(object_collision_dumbbell_gap);
    RUN_TEST(object_collision_l_shape);
    RUN_TEST(object_collision_contact_accuracy);
    RUN_TEST(object_collision_moving_head_on);
    RUN_TEST(object_collision_stacking);
    RUN_TEST(split_piece_keeps_parent_velocity);
    RUN_TEST(split_spinning_object_keeps_rigid_motion);
    RUN_TEST(split_piece_in_reused_slot_gets_parent_motion);
    RUN_TEST(new_object_in_reused_slot_starts_at_rest);
    RUN_TEST(reused_slot_does_not_find_previous_body);
    RUN_TEST(object_collision_stacking_after_world_reuse);
    RUN_TEST(hull_cache_slot_reuse);
    RUN_TEST(object_collision_crossed_planks);
    RUN_TEST(object_collision_wall_friction);
    RUN_TEST(object_collision_no_interpenetration);
    RUN_TEST(object_impact_wakes_sleeping_body);
    RUN_TEST(object_friction_does_not_reverse_sliding);
    RUN_TEST(object_contact_needs_hull_overlap);
    RUN_TEST(rotated_odd_objects_reach_narrow_phase);
    RUN_TEST(gjk_random_box_pairs);
    RUN_TEST(hull_covers_large_objects);
    RUN_TEST(hull_of_random_points);
    RUN_TEST(epa_depth_matches_sat_on_boxes);
    RUN_TEST(sap_matches_brute_force);

    printf("\n=== Resting Support Tests ===\n");
    RUN_TEST(terrain_contact_l_shape_stability);
    RUN_TEST(large_objects_rest_stable_and_sleep);
    RUN_TEST(body_on_removed_terrain_island_falls);
    RUN_TEST(dig_under_resting_body_wakes_it);

    printf("\n=== Physics Sleep & Tunneling Tests ===\n");
    RUN_TEST(physics_no_sleep_midair);
    RUN_TEST(physics_terrain_bounce);
    RUN_TEST(physics_no_tunneling);

    printf("\n=== Particle Tests ===\n");
    RUN_TEST(particles_settle_and_wake);
    RUN_TEST(particles_settled_debris_expires);
    RUN_TEST(particles_full_system_replaces_settled_first);
    RUN_TEST(particles_pinned_debris_settles);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
