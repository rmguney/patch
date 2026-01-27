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
    a[0] = 1.0f;
    a[1] = 2.0f;
    a[2] = 3.0f;
    a[3] = 4.0f;
    a[4] = 5.0f;
    a[5] = 6.0f;
    a[6] = 7.0f;
    a[7] = 8.0f;
    a[8] = 9.0f;

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

TEST(physics_terrain_contact)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);

    Vec3 floor_min = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    Vec3 floor_max = vec3_create(bounds.max_x, bounds.min_y + 0.5f, bounds.max_z);
    volume_fill_box(terrain, floor_min, floor_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = voxel_object_world_add_box(obj_world,
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

TEST(physics_terrain_settling)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 8.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);

    Vec3 floor_min = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    Vec3 floor_max = vec3_create(bounds.max_x, bounds.min_y + 0.5f, bounds.max_z);
    volume_fill_box(terrain, floor_min, floor_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = voxel_object_world_add_box(obj_world,
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

    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);

    Vec3 floor_min = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    Vec3 floor_max = vec3_create(bounds.max_x, bounds.min_y + 0.5f, bounds.max_z);
    volume_fill_box(terrain, floor_min, floor_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = voxel_object_world_add_box(obj_world,
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

TEST(object_collision_basic)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_a = voxel_object_world_add_box(obj_world, vec3_create(-0.3f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = voxel_object_world_add_box(obj_world, vec3_create(0.3f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    int32_t body_a = physics_world_add_body(physics, obj_a);
    int32_t body_b = physics_world_add_body(physics, obj_b);
    ASSERT(body_a >= 0 && body_b >= 0);

    RigidBody *rb_a = physics_world_get_body(physics, body_a);
    RigidBody *rb_b = physics_world_get_body(physics, body_b);
    rb_a->flags |= PHYS_FLAG_KINEMATIC;
    rb_b->flags |= PHYS_FLAG_KINEMATIC;
    rb_a->flags &= ~PHYS_FLAG_KINEMATIC;
    rb_b->flags &= ~PHYS_FLAG_KINEMATIC;

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

    ASSERT(final_dist >= initial_dist - 0.1f);

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

    obj->voxel_revision = 1;
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

    obj->voxel_revision = 1;
    voxel_object_recalc_shape(obj);
    return slot;
}

TEST(object_collision_l_shape)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t l_shape = create_l_shape_object(obj_world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(l_shape >= 0);

    int32_t small_box = create_small_box_object(obj_world, vec3_create(0.5f, 10.0f + 1.0f, 0.5f), MAT_STONE);
    ASSERT(small_box >= 0);

    int32_t body_l = physics_world_add_body(physics, l_shape);
    int32_t body_small = physics_world_add_body(physics, small_box);
    ASSERT(body_l >= 0 && body_small >= 0);

    RigidBody *rb_l = physics_world_get_body(physics, body_l);
    rb_l->flags |= PHYS_FLAG_STATIC;

    RigidBody *rb_small = physics_world_get_body(physics, body_small);
    rb_small->velocity = vec3_create(0.0f, -2.0f, 0.0f);

    float initial_y = obj_world->objects[small_box].position.y;

    for (int32_t tick = 0; tick < 60; tick++)
    {
        physics_world_step(physics, 1.0f / 60.0f);
    }

    float final_y = obj_world->objects[small_box].position.y;

    printf("(initial_y=%.2f, final_y=%.2f) ", initial_y, final_y);

    ASSERT(final_y < initial_y - 0.5f);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    return 1;
}

TEST(object_collision_contact_accuracy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_a = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = voxel_object_world_add_box(obj_world, vec3_create(0.95f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    int32_t body_a = physics_world_add_body(physics, obj_a);
    int32_t body_b = physics_world_add_body(physics, obj_b);
    ASSERT(body_a >= 0 && body_b >= 0);

    ObjectCollisionPair pairs[PHYS_OBJ_COLLISION_BUDGET];
    int32_t pair_count = physics_detect_object_pairs(physics, pairs, PHYS_OBJ_COLLISION_BUDGET);

    printf("(pair_count=%d) ", pair_count);

    if (pair_count > 0)
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

    int32_t obj_a = voxel_object_world_add_box(obj_world, vec3_create(-3.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = voxel_object_world_add_box(obj_world, vec3_create(3.0f, 10.0f, 0.0f),
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

TEST(object_collision_stacking)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, 0.25f);
    PhysicsWorld *physics = physics_world_create(obj_world, NULL);

    int32_t obj_base = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 5.0f, 0.0f),
                                                  vec3_create(1.0f, 0.5f, 1.0f), MAT_STONE);
    int32_t obj_top = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 7.0f, 0.0f),
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

    obj->voxel_revision = 1;
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

    int32_t box_idx = voxel_object_world_add_box(obj_world,
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

    int32_t obj_a = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    int32_t obj_b = voxel_object_world_add_box(obj_world, vec3_create(0.5f, 10.0f, 0.0f),
                                               vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(obj_a >= 0 && obj_b >= 0);

    int32_t body_a = physics_world_add_body(physics, obj_a);
    int32_t body_b = physics_world_add_body(physics, obj_b);
    ASSERT(body_a >= 0 && body_b >= 0);

    RigidBody *rb_a = physics_world_get_body(physics, body_a);
    RigidBody *rb_b = physics_world_get_body(physics, body_b);
    rb_a->flags |= PHYS_FLAG_STATIC;
    rb_b->flags |= PHYS_FLAG_STATIC;
    rb_a->flags &= ~PHYS_FLAG_STATIC;
    rb_b->flags &= ~PHYS_FLAG_STATIC;

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

    int32_t obj_idx = voxel_object_world_add_box(obj_world, vec3_create(0.0f, 30.0f, 0.0f),
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

    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);

    Vec3 floor_min = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    Vec3 floor_max = vec3_create(bounds.max_x, bounds.min_y + 0.5f, bounds.max_z);
    volume_fill_box(terrain, floor_min, floor_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    float floor_surface = bounds.min_y + 0.5f;
    int32_t obj_idx = voxel_object_world_add_box(obj_world,
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
    volume_fill_box(terrain, wall_min, wall_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

    VoxelObjectWorld *obj_world = voxel_object_world_create(bounds, voxel_size);
    PhysicsWorld *physics = physics_world_create(obj_world, terrain);

    int32_t obj_idx = voxel_object_world_add_box(obj_world,
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

TEST(collider_box_count_basic)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = voxel_object_world_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    VoxelObject *obj = &world->objects[obj_idx];
    printf("(boxes=%d, voxels=%d) ", obj->collider_box_count, obj->voxel_count);

    ASSERT(obj->collider_box_count == 1);

    ColliderBox *box = &obj->collider_boxes[0];
    float vol_x = box->local_max.x - box->local_min.x;
    float vol_y = box->local_max.y - box->local_min.y;
    float vol_z = box->local_max.z - box->local_min.z;
    float box_volume = vol_x * vol_y * vol_z;
    float vs = obj->voxel_size;
    float voxel_volume = (float)obj->voxel_count * vs * vs * vs;
    float ratio = box_volume / voxel_volume;
    ASSERT(ratio > 0.99f && ratio < 1.01f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(collider_box_l_shape)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = create_l_shape_object(world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    VoxelObject *obj = &world->objects[obj_idx];
    printf("(boxes=%d, voxels=%d) ", obj->collider_box_count, obj->voxel_count);

    ASSERT(obj->collider_box_count >= 2);
    ASSERT(obj->collider_box_count <= 10);

    float vs = obj->voxel_size;
    float total_box_volume = 0.0f;
    for (int32_t i = 0; i < obj->collider_box_count; i++)
    {
        ColliderBox *box = &obj->collider_boxes[i];
        float dx = box->local_max.x - box->local_min.x;
        float dy = box->local_max.y - box->local_min.y;
        float dz = box->local_max.z - box->local_min.z;
        ASSERT(dx > 0.0f && dy > 0.0f && dz > 0.0f);
        total_box_volume += dx * dy * dz;
    }
    float voxel_volume = (float)obj->voxel_count * vs * vs * vs;
    float ratio = total_box_volume / voxel_volume;
    printf("(vol_ratio=%.3f) ", ratio);
    ASSERT(ratio > 0.99f && ratio < 1.01f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(collider_box_dumbbell)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = create_dumbbell_object(world, vec3_create(0.0f, 10.0f, 0.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    VoxelObject *obj = &world->objects[obj_idx];
    printf("(boxes=%d, voxels=%d) ", obj->collider_box_count, obj->voxel_count);

    ASSERT(obj->collider_box_count >= 3);

    float vs = obj->voxel_size;
    float total_box_volume = 0.0f;
    for (int32_t i = 0; i < obj->collider_box_count; i++)
    {
        ColliderBox *box = &obj->collider_boxes[i];
        float dx = box->local_max.x - box->local_min.x;
        float dy = box->local_max.y - box->local_min.y;
        float dz = box->local_max.z - box->local_min.z;
        total_box_volume += dx * dy * dz;
    }
    float voxel_volume = (float)obj->voxel_count * vs * vs * vs;
    float ratio = total_box_volume / voxel_volume;
    printf("(vol_ratio=%.3f) ", ratio);
    ASSERT(ratio > 0.99f && ratio < 1.01f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(terrain_contact_l_shape_stability)
{
    Bounds3D bounds = {-5.0f, 5.0f, 0.0f, 20.0f, -5.0f, 5.0f};
    float voxel_size = 0.1f;

    Vec3 origin = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    VoxelVolume *terrain = volume_create_dims(4, 4, 4, origin, voxel_size);
    Vec3 floor_min = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    Vec3 floor_max = vec3_create(bounds.max_x, bounds.min_y + 0.5f, bounds.max_z);
    volume_fill_box(terrain, floor_min, floor_max, MAT_STONE);
    volume_rebuild_all_occupancy(terrain);

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

    printf("(settled=%d, y=%.2f, boxes=%d) ", settled_tick, final_y, obj->collider_box_count);

    ASSERT(settled_tick >= 0);
    ASSERT(above_floor);
    ASSERT(below_start);

    physics_world_destroy(physics);
    voxel_object_world_destroy(obj_world);
    volume_destroy(terrain);
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
    RUN_TEST(physics_terrain_contact);
    RUN_TEST(physics_terrain_settling);
    RUN_TEST(physics_no_jitter_after_sleep);
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

    printf("\n=== Object-Object Collision Tests ===\n");
    RUN_TEST(object_collision_basic);
    RUN_TEST(object_collision_dumbbell_gap);
    RUN_TEST(object_collision_l_shape);
    RUN_TEST(object_collision_contact_accuracy);
    RUN_TEST(object_collision_moving_head_on);
    RUN_TEST(object_collision_stacking);
    RUN_TEST(object_collision_wall_friction);
    RUN_TEST(object_collision_no_interpenetration);

    printf("\n=== Compound Collider Tests ===\n");
    RUN_TEST(collider_box_count_basic);
    RUN_TEST(collider_box_l_shape);
    RUN_TEST(collider_box_dumbbell);
    RUN_TEST(terrain_contact_l_shape_stability);

    printf("\n=== Physics Sleep & Tunneling Tests ===\n");
    RUN_TEST(physics_no_sleep_midair);
    RUN_TEST(physics_terrain_bounce);
    RUN_TEST(physics_no_tunneling);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
