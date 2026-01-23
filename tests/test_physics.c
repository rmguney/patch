#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/voxel_object.h"
#include "engine/sim/detach.h"
#include "engine/voxel/volume.h"
#include "engine/physics/rigidbody.h"
#include "engine/physics/collision_object.h"
#include "engine/physics/character.h"
#include "engine/physics/projectile.h"
#include "engine/physics/ragdoll.h"
#include "engine/platform/platform.h"
#include "content/materials.h"
#include "test_common.h"
#include <string.h>
#include <math.h>

TEST(mat3_multiply_identity)
{
    float id[9], a[9], out[9];
    mat3_identity(id);
    a[0] = 1.0f; a[1] = 2.0f; a[2] = 3.0f;
    a[3] = 4.0f; a[4] = 5.0f; a[5] = 6.0f;
    a[6] = 7.0f; a[7] = 8.0f; a[8] = 9.0f;

    mat3_multiply(a, id, out);
    for (int i = 0; i < 9; i++)
        ASSERT_NEAR(out[i], a[i], K_EPSILON);

    mat3_multiply(id, a, out);
    for (int i = 0; i < 9; i++)
        ASSERT_NEAR(out[i], a[i], K_EPSILON);
    return 1;
}

TEST(mat3_transpose_unit)
{
    float m[9], t[9];
    m[0] = 1.0f; m[1] = 2.0f; m[2] = 3.0f;
    m[3] = 4.0f; m[4] = 5.0f; m[5] = 6.0f;
    m[6] = 7.0f; m[7] = 8.0f; m[8] = 9.0f;

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

    int32_t idx = voxel_object_world_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
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

    int32_t idx = voxel_object_world_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                              vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(idx >= 0);
    VoxelObject *obj = &world->objects[idx];

    float max_extent = obj->shape_half_extents.x;
    if (obj->shape_half_extents.y > max_extent) max_extent = obj->shape_half_extents.y;
    if (obj->shape_half_extents.z > max_extent) max_extent = obj->shape_half_extents.z;
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

TEST(object_raycast_hit)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = voxel_object_world_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
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

    voxel_object_world_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                               vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);

    Vec3 origin = vec3_create(10.0f, 20.0f, 0.0f);
    Vec3 dir = vec3_create(0.0f, 1.0f, 0.0f);

    VoxelObjectHit hit = voxel_object_world_raycast(world, origin, dir);

    ASSERT(hit.hit == false);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(quat_from_axis_angle_identity)
{
    Quat q = quat_from_axis_angle(vec3_create(0.0f, 1.0f, 0.0f), 0.0f);
    ASSERT_NEAR(q.w, 1.0f, K_EPSILON);
    ASSERT_NEAR(q.x, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.y, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.z, 0.0f, K_EPSILON);
    return 1;
}

TEST(quat_rotate_vec3_y_axis)
{
    Quat q = quat_from_axis_angle(vec3_create(0.0f, 1.0f, 0.0f), K_PI * 0.5f);
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
    ASSERT(physics_world_get_body_count(physics) == 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_body_add_remove)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    int32_t body_idx = physics_world_add_body(physics, obj_idx);
    ASSERT(body_idx >= 0);
    ASSERT(physics_world_get_body_count(physics) == 1);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body != NULL);
    ASSERT((body->flags & PHYS_FLAG_ACTIVE) != 0);
    ASSERT(body->mass > 0.0f);

    physics_world_remove_body(physics, body_idx);
    ASSERT(physics_world_get_body_count(physics) == 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_body_inertia_symmetric)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body != NULL);

    ASSERT(fabsf(body->inertia_local.x - body->inertia_local.y) < 0.1f);
    ASSERT(fabsf(body->inertia_local.y - body->inertia_local.z) < 0.1f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_integration_gravity)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
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

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT((body->flags & PHYS_FLAG_SLEEPING) == 0);

    body->velocity = vec3_zero();
    body->angular_velocity = vec3_zero();
    body->sleep_frames = PHYS_SLEEP_FRAMES;
    body->flags |= PHYS_FLAG_SLEEPING;

    ASSERT(physics_body_is_sleeping(physics, body_idx) == true);

    physics_body_wake(physics, body_idx);
    ASSERT(physics_body_is_sleeping(physics, body_idx) == false);
    ASSERT(body->sleep_frames == 0);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_wake_on_impulse)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    body->flags |= PHYS_FLAG_SLEEPING;
    body->sleep_frames = PHYS_SLEEP_FRAMES;

    Vec3 impulse = vec3_create(5.0f, 0.0f, 0.0f);
    Vec3 point = obj_world->objects[obj_idx].position;
    physics_body_apply_impulse(physics, body_idx, impulse, point);

    ASSERT(physics_body_is_sleeping(physics, body_idx) == false);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_velocity_clamping)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
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

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    physics_body_set_velocity(physics, body_idx, vec3_create(5.0f, 0.0f, 0.0f));
    physics_body_set_angular_velocity(physics, body_idx, vec3_create(0.0f, 2.0f, 0.0f));

    RigidBody *body = physics_world_get_body(physics, body_idx);
    float initial_ke = 0.5f * body->mass * vec3_length_sq(body->velocity);
    initial_ke += 0.5f * vec3_dot(body->inertia_local, vec3_mul(body->angular_velocity, body->angular_velocity));

    for (int32_t i = 0; i < 60; i++)
    {
        body->velocity.y = 0.0f;
        physics_world_step(physics, 1.0f / 60.0f);
    }

    float final_ke = 0.5f * body->mass * vec3_length_sq(body->velocity);
    final_ke += 0.5f * vec3_dot(body->inertia_local, vec3_mul(body->angular_velocity, body->angular_velocity));

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

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body->mass > 0.0f);
    ASSERT(body->inv_mass > 0.0f);
    ASSERT(body->inv_mass < 1e6f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_performance_128_bodies)
{
    Bounds3D bounds = {-64.0f, 64.0f, 0.0f, 128.0f, -64.0f, 64.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    for (int32_t i = 0; i < 128; i++)
    {
        float x = (float)(i % 8) * 4.0f - 14.0f;
        float y = 20.0f + (float)(i / 64) * 4.0f;
        float z = (float)((i / 8) % 8) * 4.0f - 14.0f;

        int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(x, y, z),
                                                      vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
        if (obj_idx >= 0)
            physics_world_add_body(physics, obj_idx);
    }

    printf("(created %d bodies) ", physics_world_get_body_count(physics));

    PlatformTime t0 = platform_time_now();

    for (int32_t tick = 0; tick < 1000; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    PlatformTime t1 = platform_time_now();
    float elapsed_ms = platform_time_delta_seconds(t0, t1) * 1000.0f;

    printf("(1000 ticks in %.1fms) ", elapsed_ms);

    ASSERT(elapsed_ms < 500.0f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(character_init_defaults)
{
    Character character;
    character_init(&character, vec3_create(0.0f, 10.0f, 0.0f));

    ASSERT_NEAR(character.position.x, 0.0f, K_EPSILON);
    ASSERT_NEAR(character.position.y, 10.0f, K_EPSILON);
    ASSERT_NEAR(character.position.z, 0.0f, K_EPSILON);
    ASSERT_NEAR(character.radius, CHAR_CAPSULE_RADIUS, K_EPSILON);
    ASSERT_NEAR(character.height, CHAR_CAPSULE_HEIGHT, K_EPSILON);
    ASSERT(character.is_grounded == false);

    return 1;
}

TEST(character_gravity_fall)
{
    Character character;
    character_init(&character, vec3_create(0.0f, 10.0f, 0.0f));

    float initial_y = character.position.y;

    character_move(&character, NULL, NULL, vec3_zero(), 1.0f / 60.0f);

    ASSERT(character.position.y < initial_y);
    ASSERT(character.velocity.y < 0.0f);

    return 1;
}

TEST(projectile_system_create_destroy)
{
    ProjectileSystem *system = projectile_system_create();
    ASSERT(system != NULL);
    ASSERT(projectile_system_active_count(system) == 0);

    projectile_system_destroy(system);
    return 1;
}

TEST(projectile_ballistic_fire)
{
    ProjectileSystem *system = projectile_system_create();

    Vec3 origin = vec3_create(0.0f, 10.0f, 0.0f);
    Vec3 velocity = vec3_create(10.0f, 5.0f, 0.0f);

    int32_t proj_idx = projectile_fire_ballistic(system, origin, velocity, 1.0f, 0.1f, 10.0f);
    ASSERT(proj_idx >= 0);
    ASSERT(projectile_system_active_count(system) == 1);

    Projectile *proj = projectile_system_get(system, proj_idx);
    ASSERT(proj != NULL);
    ASSERT(proj->active == true);
    ASSERT_NEAR(proj->position.x, origin.x, K_EPSILON);
    ASSERT_NEAR(proj->position.y, origin.y, K_EPSILON);

    projectile_system_destroy(system);
    return 1;
}

TEST(projectile_ballistic_trajectory)
{
    ProjectileSystem *system = projectile_system_create();

    Vec3 origin = vec3_create(0.0f, 10.0f, 0.0f);
    Vec3 velocity = vec3_create(10.0f, 0.0f, 0.0f);

    int32_t proj_idx = projectile_fire_ballistic(system, origin, velocity, 1.0f, 0.1f, 10.0f);
    Projectile *proj = projectile_system_get(system, proj_idx);

    float initial_y = proj->position.y;

    projectile_system_update(system, NULL, NULL, 1.0f / 60.0f, NULL, NULL, 0);

    ASSERT(proj->position.x > origin.x);
    ASSERT(proj->position.y < initial_y);

    projectile_system_destroy(system);
    return 1;
}

TEST(projectile_lifetime_expiry)
{
    ProjectileSystem *system = projectile_system_create();

    Vec3 origin = vec3_create(0.0f, 100.0f, 0.0f);
    Vec3 velocity = vec3_create(1.0f, 0.0f, 0.0f);

    projectile_fire_ballistic(system, origin, velocity, 1.0f, 0.1f, 0.5f);
    ASSERT(projectile_system_active_count(system) == 1);

    for (int32_t i = 0; i < 60; i++)
    {
        projectile_system_update(system, NULL, NULL, 1.0f / 60.0f, NULL, NULL, 0);
    }

    ASSERT(projectile_system_active_count(system) == 0);

    projectile_system_destroy(system);
    return 1;
}

TEST(physics_static_flag)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
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

TEST(physics_kinematic_flag)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    int32_t body_idx = physics_world_add_body(physics, obj_idx);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    body->flags |= PHYS_FLAG_KINEMATIC;

    VoxelObject *obj = &obj_world->objects[obj_idx];
    float initial_y = obj->position.y;

    physics_world_step(physics, 1.0f / 60.0f);

    ASSERT_NEAR(obj->position.y, initial_y, 0.01f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(physics_add_body_with_mass)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);

    float custom_mass = 100.0f;
    Vec3 half_extents = vec3_create(1.0f, 1.0f, 1.0f);
    int32_t body_idx = physics_world_add_body_with_mass(physics, obj_idx, custom_mass, half_extents);

    ASSERT(body_idx >= 0);

    RigidBody *body = physics_world_get_body(physics, body_idx);
    ASSERT(body != NULL);
    ASSERT_NEAR(body->mass, custom_mass, K_EPSILON);
    ASSERT_NEAR(body->inv_mass, 1.0f / custom_mass, K_EPSILON);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(ragdoll_system_create_destroy)
{
    RagdollSystem *system = ragdoll_system_create();
    ASSERT(system != NULL);
    ASSERT(ragdoll_system_active_count(system) == 0);

    ragdoll_system_destroy(system);
    return 1;
}

TEST(ragdoll_spawn_despawn)
{
    RagdollSystem *system = ragdoll_system_create();

    Vec3 position = vec3_create(0.0f, 10.0f, 0.0f);
    int32_t ragdoll_idx = ragdoll_spawn(system, position, 1.0f);
    ASSERT(ragdoll_idx >= 0);
    ASSERT(ragdoll_system_active_count(system) == 1);

    Ragdoll *ragdoll = ragdoll_get(system, ragdoll_idx);
    ASSERT(ragdoll != NULL);
    ASSERT(ragdoll->active == true);
    ASSERT(ragdoll->part_count == RAGDOLL_MAX_PARTS);
    ASSERT(ragdoll->constraint_count == 5);

    ragdoll_despawn(system, ragdoll_idx);
    ASSERT(ragdoll_system_active_count(system) == 0);
    ASSERT(ragdoll_get(system, ragdoll_idx) == NULL);

    ragdoll_system_destroy(system);
    return 1;
}

TEST(ragdoll_update_gravity)
{
    RagdollSystem *system = ragdoll_system_create();

    Vec3 position = vec3_create(0.0f, 10.0f, 0.0f);
    int32_t ragdoll_idx = ragdoll_spawn(system, position, 1.0f);

    Ragdoll *ragdoll = ragdoll_get(system, ragdoll_idx);
    float initial_y = ragdoll->parts[RAGDOLL_PART_TORSO].position.y;

    ragdoll_system_update(system, NULL, 1.0f / 60.0f);

    ASSERT(ragdoll->parts[RAGDOLL_PART_TORSO].position.y < initial_y);

    ragdoll_system_destroy(system);
    return 1;
}

TEST(ragdoll_apply_impulse)
{
    RagdollSystem *system = ragdoll_system_create();

    Vec3 position = vec3_create(0.0f, 10.0f, 0.0f);
    int32_t ragdoll_idx = ragdoll_spawn(system, position, 1.0f);

    Ragdoll *ragdoll = ragdoll_get(system, ragdoll_idx);
    float initial_x = ragdoll->parts[RAGDOLL_PART_HEAD].position.x;

    Vec3 impulse = vec3_create(10.0f, 0.0f, 0.0f);
    ragdoll_apply_impulse(system, ragdoll_idx, RAGDOLL_PART_HEAD, impulse);

    ASSERT(ragdoll->parts[RAGDOLL_PART_HEAD].position.x > initial_x);

    ragdoll_system_destroy(system);
    return 1;
}

int main(void)
{
    platform_time_init();

    printf("=== Matrix Math Tests ===\n");
    RUN_TEST(mat3_multiply_identity);
    RUN_TEST(mat3_transpose_unit);

    printf("\n=== Quaternion/Vector Math Tests ===\n");
    RUN_TEST(quat_from_axis_angle_identity);
    RUN_TEST(quat_rotate_vec3_y_axis);
    RUN_TEST(vec3_clamp_length_under_limit);
    RUN_TEST(vec3_clamp_length_over_limit);

    printf("\n=== Voxel Object Shape Tests ===\n");
    RUN_TEST(shape_half_extents_symmetric);
    RUN_TEST(bounding_sphere_accuracy);

    printf("\n=== Object Fragmentation Tests ===\n");
    RUN_TEST(object_split_creates_fragments);
    RUN_TEST(object_split_dumbbell);

    printf("\n=== Object Raycast Tests ===\n");
    RUN_TEST(object_raycast_hit);
    RUN_TEST(object_raycast_miss);

    printf("\n=== Rigid Body Physics Tests ===\n");
    RUN_TEST(physics_world_create_destroy);
    RUN_TEST(physics_body_add_remove);
    RUN_TEST(physics_body_inertia_symmetric);
    RUN_TEST(physics_integration_gravity);
    RUN_TEST(physics_sleep_from_rest);
    RUN_TEST(physics_wake_on_impulse);
    RUN_TEST(physics_velocity_clamping);
    RUN_TEST(physics_energy_decreases);
    RUN_TEST(physics_zero_mass_guard);
    RUN_TEST(physics_performance_128_bodies);

    printf("\n=== Character Controller Tests ===\n");
    RUN_TEST(character_init_defaults);
    RUN_TEST(character_gravity_fall);

    printf("\n=== Projectile Tests ===\n");
    RUN_TEST(projectile_system_create_destroy);
    RUN_TEST(projectile_ballistic_fire);
    RUN_TEST(projectile_ballistic_trajectory);
    RUN_TEST(projectile_lifetime_expiry);

    printf("\n=== Physics Flags Tests ===\n");
    RUN_TEST(physics_static_flag);
    RUN_TEST(physics_kinematic_flag);
    RUN_TEST(physics_add_body_with_mass);

    printf("\n=== Ragdoll Tests ===\n");
    RUN_TEST(ragdoll_system_create_destroy);
    RUN_TEST(ragdoll_spawn_despawn);
    RUN_TEST(ragdoll_update_gravity);
    RUN_TEST(ragdoll_apply_impulse);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
