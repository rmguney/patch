#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/core/profile.h"
#include "engine/physics/physics_step.h"
#include "engine/physics/voxel_body.h"
#include "engine/voxel/volume.h"
#include "engine/platform/platform.h"
#include "content/materials.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name)             \
    do                             \
    {                              \
        g_tests_run++;             \
        printf("  %s... ", #name); \
        if (test_##name())         \
        {                          \
            g_tests_passed++;      \
            printf("PASS\n");      \
        }                          \
        else                       \
        {                          \
            printf("FAIL\n");      \
        }                          \
    } while (0)

#define ASSERT(cond)                              \
    do                                            \
    {                                             \
        if (!(cond))                              \
        {                                         \
            printf("ASSERT FAILED: %s\n", #cond); \
            return 0;                             \
        }                                         \
    } while (0)
#define ASSERT_NEAR(a, b, eps)                                                \
    do                                                                        \
    {                                                                         \
        if (fabsf((a) - (b)) > (eps))                                         \
        {                                                                     \
            printf("ASSERT_NEAR FAILED: %f != %f\n", (float)(a), (float)(b)); \
            return 0;                                                         \
        }                                                                     \
    } while (0)

TEST(quat_identity_unit)
{
    Quat q = quat_identity();
    ASSERT_NEAR(q.x, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.y, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.z, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.w, 1.0f, K_EPSILON);
    ASSERT_NEAR(quat_length(q), 1.0f, K_EPSILON);
    return 1;
}

TEST(quat_normalize_unit)
{
    Quat q = quat_create(1.0f, 2.0f, 3.0f, 4.0f);
    q = quat_normalize(q);
    ASSERT_NEAR(quat_length(q), 1.0f, K_EPSILON);

    Quat q2 = quat_create(0.1f, 0.2f, 0.3f, 0.4f);
    q2 = quat_normalize(q2);
    ASSERT_NEAR(quat_length(q2), 1.0f, K_EPSILON);
    return 1;
}

TEST(quat_from_axis_angle_unit)
{
    Vec3 axis = vec3_create(0.0f, 1.0f, 0.0f);
    Quat q = quat_from_axis_angle(axis, K_PI * 0.5f);
    ASSERT_NEAR(quat_length(q), 1.0f, K_EPSILON);

    float expected_sin = sinf(K_PI * 0.25f);
    float expected_cos = cosf(K_PI * 0.25f);
    ASSERT_NEAR(q.x, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.y, expected_sin, K_EPSILON);
    ASSERT_NEAR(q.z, 0.0f, K_EPSILON);
    ASSERT_NEAR(q.w, expected_cos, K_EPSILON);
    return 1;
}

TEST(quat_multiply_identity)
{
    Vec3 axis = vec3_create(1.0f, 0.0f, 0.0f);
    Quat q = quat_from_axis_angle(axis, 0.5f);
    Quat id = quat_identity();

    Quat r1 = quat_multiply(q, id);
    ASSERT_NEAR(r1.x, q.x, K_EPSILON);
    ASSERT_NEAR(r1.y, q.y, K_EPSILON);
    ASSERT_NEAR(r1.z, q.z, K_EPSILON);
    ASSERT_NEAR(r1.w, q.w, K_EPSILON);

    Quat r2 = quat_multiply(id, q);
    ASSERT_NEAR(r2.x, q.x, K_EPSILON);
    ASSERT_NEAR(r2.y, q.y, K_EPSILON);
    ASSERT_NEAR(r2.z, q.z, K_EPSILON);
    ASSERT_NEAR(r2.w, q.w, K_EPSILON);
    return 1;
}

TEST(quat_multiply_composition)
{
    Vec3 axis_y = vec3_create(0.0f, 1.0f, 0.0f);
    Quat q1 = quat_from_axis_angle(axis_y, K_PI * 0.5f);
    Quat q2 = quat_from_axis_angle(axis_y, K_PI * 0.5f);
    Quat combined = quat_multiply(q1, q2);
    Quat expected = quat_from_axis_angle(axis_y, K_PI);

    ASSERT_NEAR(fabsf(combined.x) - fabsf(expected.x), 0.0f, K_EPSILON);
    ASSERT_NEAR(fabsf(combined.y) - fabsf(expected.y), 0.0f, K_EPSILON);
    ASSERT_NEAR(fabsf(combined.z) - fabsf(expected.z), 0.0f, K_EPSILON);
    ASSERT_NEAR(fabsf(combined.w) - fabsf(expected.w), 0.0f, K_EPSILON);
    return 1;
}

TEST(quat_integrate_unit)
{
    Quat q = quat_identity();
    Vec3 angular_velocity = vec3_create(0.0f, 1.0f, 0.0f);
    float dt = 0.01f;

    for (int i = 0; i < 100; i++)
    {
        quat_integrate(&q, angular_velocity, dt);
    }

    ASSERT_NEAR(quat_length(q), 1.0f, K_EPSILON);
    return 1;
}

TEST(quat_to_mat3_identity)
{
    Quat q = quat_identity();
    float m[9];
    quat_to_mat3(q, m);

    ASSERT_NEAR(m[0], 1.0f, K_EPSILON);
    ASSERT_NEAR(m[1], 0.0f, K_EPSILON);
    ASSERT_NEAR(m[2], 0.0f, K_EPSILON);
    ASSERT_NEAR(m[3], 0.0f, K_EPSILON);
    ASSERT_NEAR(m[4], 1.0f, K_EPSILON);
    ASSERT_NEAR(m[5], 0.0f, K_EPSILON);
    ASSERT_NEAR(m[6], 0.0f, K_EPSILON);
    ASSERT_NEAR(m[7], 0.0f, K_EPSILON);
    ASSERT_NEAR(m[8], 1.0f, K_EPSILON);
    return 1;
}

TEST(quat_to_mat3_rotation)
{
    Vec3 axis = vec3_create(0.0f, 1.0f, 0.0f);
    Quat q = quat_from_axis_angle(axis, K_PI * 0.5f);
    float m[9];
    quat_to_mat3(q, m);

    Vec3 input = vec3_create(1.0f, 0.0f, 0.0f);
    Vec3 result = mat3_transform_vec3(m, input);

    ASSERT_NEAR(result.x, 0.0f, K_EPSILON);
    ASSERT_NEAR(result.y, 0.0f, K_EPSILON);
    ASSERT_NEAR(result.z, -1.0f, K_EPSILON);
    return 1;
}

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

TEST(physics_state_init_destroy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    ASSERT(state.proxy_count == 0);
    ASSERT(state.fragment_count == 0);
    ASSERT(state.gravity.y < 0); /* Should have downward gravity */
    ASSERT(state.floor_y == bounds.min_y);

    physics_state_destroy(&state);
    return 1;
}

TEST(proxy_alloc_free)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    /* Allocate a proxy */
    int32_t idx = physics_proxy_alloc(&state);
    ASSERT(idx >= 0);
    ASSERT(state.proxy_count == 1);

    PhysicsProxy *proxy = physics_proxy_get(&state, idx);
    ASSERT(proxy != NULL);
    ASSERT(proxy->active);

    /* Free the proxy */
    physics_proxy_free(&state, idx);
    ASSERT(state.proxy_count == 0);

    /* Should not be gettable anymore */
    proxy = physics_proxy_get(&state, idx);
    ASSERT(proxy == NULL);

    physics_state_destroy(&state);
    return 1;
}

TEST(proxy_free_list_reuse)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    /* Allocate three proxies */
    int32_t idx0 = physics_proxy_alloc(&state);
    int32_t idx1 = physics_proxy_alloc(&state);
    int32_t idx2 = physics_proxy_alloc(&state);
    ASSERT(state.proxy_count == 3);

    /* Free the middle one */
    physics_proxy_free(&state, idx1);
    ASSERT(state.proxy_count == 2);

    /* Allocate again - should reuse freed slot */
    int32_t idx3 = physics_proxy_alloc(&state);
    ASSERT(idx3 == idx1); /* Free list gives back same slot */
    ASSERT(state.proxy_count == 3);

    physics_state_destroy(&state);
    return 1;
}

TEST(proxy_gravity)
{
    Bounds3D bounds = {-100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 100.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    int32_t idx = physics_proxy_alloc(&state);
    PhysicsProxy *proxy = physics_proxy_get(&state, idx);
    ASSERT(proxy != NULL);

    proxy->position = vec3_create(0.0f, 50.0f, 0.0f);
    proxy->velocity = vec3_zero();
    proxy->half_extents = vec3_create(1.0f, 1.0f, 1.0f);
    proxy->shape = PROXY_SHAPE_SPHERE;
    proxy->flags = PROXY_FLAG_GRAVITY;

    float initial_y = proxy->position.y;

    RngState rng;
    rng_seed(&rng, 12345);

    /* Step physics */
    float dt = 1.0f / 60.0f;
    physics_step(&state, dt, &rng);

    /* Proxy should have moved down */
    ASSERT(proxy->position.y < initial_y);
    ASSERT(proxy->velocity.y < 0.0f);

    physics_state_destroy(&state);
    return 1;
}

TEST(proxy_floor_collision)
{
    Bounds3D bounds = {-100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 100.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    int32_t idx = physics_proxy_alloc(&state);
    PhysicsProxy *proxy = physics_proxy_get(&state, idx);

    proxy->position = vec3_create(0.0f, 1.0f, 0.0f);   /* Just above floor */
    proxy->velocity = vec3_create(0.0f, -10.0f, 0.0f); /* Moving down */
    proxy->half_extents = vec3_create(0.5f, 0.5f, 0.5f);
    proxy->shape = PROXY_SHAPE_SPHERE;
    proxy->flags = PROXY_FLAG_GRAVITY;
    proxy->restitution = 0.5f;

    RngState rng;
    rng_seed(&rng, 12345);

    /* Step physics multiple times */
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; i++)
    {
        physics_step(&state, dt, &rng);
    }

    /* Proxy should have settled near floor */
    float floor_y = bounds.min_y + proxy->half_extents.x; /* sphere radius */
    ASSERT(proxy->position.y >= floor_y - 0.1f);
    ASSERT(proxy->grounded);

    physics_state_destroy(&state);
    return 1;
}

TEST(proxy_sphere_collision)
{
    Bounds3D bounds = {-100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 100.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    /* Create two overlapping spheres */
    int32_t idx1 = physics_proxy_alloc(&state);
    int32_t idx2 = physics_proxy_alloc(&state);

    PhysicsProxy *p1 = physics_proxy_get(&state, idx1);
    PhysicsProxy *p2 = physics_proxy_get(&state, idx2);

    p1->position = vec3_create(0.0f, 10.0f, 0.0f);
    p1->velocity = vec3_create(5.0f, 0.0f, 0.0f);
    p1->half_extents = vec3_create(1.0f, 1.0f, 1.0f);
    p1->shape = PROXY_SHAPE_SPHERE;
    p1->flags = PROXY_FLAG_COLLIDE_PROXY;
    p1->mass = 1.0f;

    p2->position = vec3_create(1.5f, 10.0f, 0.0f); /* Overlapping */
    p2->velocity = vec3_create(-5.0f, 0.0f, 0.0f);
    p2->half_extents = vec3_create(1.0f, 1.0f, 1.0f);
    p2->shape = PROXY_SHAPE_SPHERE;
    p2->flags = PROXY_FLAG_COLLIDE_PROXY;
    p2->mass = 1.0f;

    RngState rng;
    rng_seed(&rng, 12345);

    /* Step physics multiple times to allow collision resolution */
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 10; i++)
    {
        physics_step(&state, dt, &rng);
    }

    /* Spheres should have been pushed apart */
    float dist = vec3_length(vec3_sub(p2->position, p1->position));
    ASSERT(dist >= 1.5f); /* Should be at least somewhat separated (radii = 1+1) */

    physics_state_destroy(&state);
    return 1;
}

TEST(fragment_spawn)
{
    Bounds3D bounds = {-100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 100.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    /* Create a small voxel fragment */
    uint8_t voxels[8]; /* 2x2x2 */
    memset(voxels, MAT_STONE, sizeof(voxels));

    Vec3 origin = vec3_create(0.0f, 50.0f, 0.0f);
    Vec3 velocity = vec3_create(1.0f, 0.0f, 0.0f);

    int32_t idx = physics_fragment_spawn(&state, voxels, 2, 2, 2, origin, 1.0f, velocity);
    ASSERT(idx >= 0);
    ASSERT(state.fragment_count == 1);

    VoxelFragment *frag = physics_fragment_get(&state, idx);
    ASSERT(frag != NULL);
    ASSERT(frag->active);
    ASSERT(frag->solid_count == 8);

    physics_state_destroy(&state);
    return 1;
}

TEST(fragment_gravity)
{
    Bounds3D bounds = {-100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 100.0f};
    PhysicsState state;
    physics_state_init(&state, bounds, NULL);

    uint8_t voxels[8];
    memset(voxels, MAT_STONE, sizeof(voxels));

    Vec3 origin = vec3_create(0.0f, 50.0f, 0.0f);
    int32_t idx = physics_fragment_spawn(&state, voxels, 2, 2, 2, origin, 1.0f, vec3_zero());

    VoxelFragment *frag = physics_fragment_get(&state, idx);
    float initial_y = frag->position.y;

    RngState rng;
    rng_seed(&rng, 12345);

    /* Step physics */
    float dt = 1.0f / 60.0f;
    physics_step(&state, dt, &rng);

    /* Fragment should have moved down */
    ASSERT(frag->position.y < initial_y);
    ASSERT(frag->velocity.y < 0.0f);

    physics_state_destroy(&state);
    return 1;
}

TEST(proxy_volume_collision)
{
    /*
     * Test proxy collision with VoxelVolume solid voxels.
     * Creates a volume with a solid block, drops a proxy onto it.
     */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    /* Create a solid platform in the volume */
    Vec3 platform_min = {-4.0f, 10.0f, -4.0f};
    Vec3 platform_max = {4.0f, 12.0f, 4.0f};
    volume_fill_box(vol, platform_min, platform_max, MAT_STONE);

    /* Verify voxels exist */
    ASSERT(volume_get_at(vol, vec3_create(0.0f, 11.0f, 0.0f)) == MAT_STONE);

    /* Initialize physics with volume */
    PhysicsState state;
    physics_state_init(&state, bounds, vol);

    /* Create a proxy above the platform */
    int32_t idx = physics_proxy_alloc(&state);
    PhysicsProxy *proxy = physics_proxy_get(&state, idx);
    ASSERT(proxy != NULL);

    proxy->position = vec3_create(0.0f, 20.0f, 0.0f);  /* Above platform */
    proxy->velocity = vec3_create(0.0f, -10.0f, 0.0f); /* Falling down */
    proxy->half_extents = vec3_create(0.5f, 0.5f, 0.5f);
    proxy->shape = PROXY_SHAPE_SPHERE;
    proxy->flags = PROXY_FLAG_GRAVITY | PROXY_FLAG_COLLIDE_VOXEL;
    proxy->mass = 1.0f;
    proxy->restitution = 0.0f;

    RngState rng;
    rng_seed(&rng, 12345);

    /* Step physics until proxy reaches platform level */
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; i++)
    {
        physics_step(&state, dt, &rng);
    }

    /* Proxy should have stopped on or above the platform (y >= 12 + radius) */
    float platform_top = 12.0f + proxy->half_extents.x;
    ASSERT(proxy->position.y >= platform_top - 1.0f); /* Allow some tolerance */
    ASSERT(proxy->position.y < 20.0f);                /* Should have fallen from initial pos */

    physics_state_destroy(&state);
    volume_destroy(vol);
    return 1;
}

TEST(determinism)
{
    Bounds3D bounds = {-100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 100.0f};

    /* Create two identical physics states */
    PhysicsState state1, state2;
    physics_state_init(&state1, bounds, NULL);
    physics_state_init(&state2, bounds, NULL);

    /* Add identical proxies */
    int32_t idx1 = physics_proxy_alloc(&state1);
    int32_t idx2 = physics_proxy_alloc(&state2);

    PhysicsProxy *p1 = physics_proxy_get(&state1, idx1);
    PhysicsProxy *p2 = physics_proxy_get(&state2, idx2);

    p1->position = vec3_create(0.0f, 50.0f, 0.0f);
    p1->velocity = vec3_create(1.0f, 2.0f, 3.0f);
    p1->half_extents = vec3_create(1.0f, 1.0f, 1.0f);
    p1->shape = PROXY_SHAPE_SPHERE;
    p1->flags = PROXY_FLAG_GRAVITY;

    p2->position = p1->position;
    p2->velocity = p1->velocity;
    p2->half_extents = p1->half_extents;
    p2->shape = p1->shape;
    p2->flags = p1->flags;

    RngState rng1, rng2;
    rng_seed(&rng1, 12345);
    rng_seed(&rng2, 12345);

    /* Step both physics states multiple times */
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 100; i++)
    {
        physics_step(&state1, dt, &rng1);
        physics_step(&state2, dt, &rng2);
    }

    /* Positions must be identical */
    ASSERT_NEAR(p1->position.x, p2->position.x, 0.0001f);
    ASSERT_NEAR(p1->position.y, p2->position.y, 0.0001f);
    ASSERT_NEAR(p1->position.z, p2->position.z, 0.0001f);

    ASSERT_NEAR(p1->velocity.x, p2->velocity.x, 0.0001f);
    ASSERT_NEAR(p1->velocity.y, p2->velocity.y, 0.0001f);
    ASSERT_NEAR(p1->velocity.z, p2->velocity.z, 0.0001f);

    physics_state_destroy(&state1);
    physics_state_destroy(&state2);
    return 1;
}

TEST(settling_time)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    RngState rng;
    rng_seed(&rng, 12345);

    int32_t obj_idx = voxel_object_world_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                                                 vec3_create(1.0f, 1.0f, 1.0f),
                                                 MAT_STONE, &rng);
    ASSERT(obj_idx >= 0);
    ASSERT(world->object_count == 1);

    VoxelObject *obj = &world->objects[obj_idx];
    ASSERT(obj->active);
    ASSERT(!obj->sleeping);
    ASSERT(obj->voxel_count > 0);

    float dt = 1.0f / 60.0f;
    int max_steps = 900;
    int steps_to_ground = 0;
    int steps_to_settle = 0;

    for (int i = 0; i < max_steps; i++)
    {
        voxel_body_world_update(world, dt);

        if (!obj->active)
            break;

        if (steps_to_ground == 0 && obj->on_ground)
            steps_to_ground = i + 1;

        if (obj->sleeping)
        {
            steps_to_settle = i + 1;
            break;
        }
    }

    float settle_time = steps_to_settle * dt;
    printf("(gnd=%d, sleep=%d, time=%.2fs, y=%.1f) ",
           steps_to_ground, steps_to_settle, settle_time, obj->position.y);

    ASSERT(obj->active);
    ASSERT(obj->on_ground);
    ASSERT(obj->sleeping);
    ASSERT(settle_time < 15.0f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(collision_bounds_accuracy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);
    world->enable_object_collision = true;

    RngState rng;
    rng_seed(&rng, 12345);

    int32_t obj1_idx = voxel_object_world_add_box(world, vec3_create(-5.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f),
                                                  MAT_STONE, &rng);
    int32_t obj2_idx = voxel_object_world_add_box(world, vec3_create(5.0f, 10.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f),
                                                  MAT_STONE, &rng);
    ASSERT(obj1_idx >= 0 && obj2_idx >= 0);

    VoxelObject *obj1 = &world->objects[obj1_idx];
    VoxelObject *obj2 = &world->objects[obj2_idx];

    obj1->velocity = vec3_create(0.5f, 0.0f, 0.0f);
    obj2->velocity = vec3_create(-0.5f, 0.0f, 0.0f);

    float dt = 1.0f / 60.0f;
    float initial_dist = fabsf(obj2->position.x - obj1->position.x);

    for (int i = 0; i < 600; i++)
    {
        voxel_body_world_update(world, dt);
    }

    float final_dist = fabsf(obj2->position.x - obj1->position.x);
    float combined_radius = obj1->radius + obj2->radius;

    printf("(dist=%.2f, radii=%.2f) ", final_dist, combined_radius);

    ASSERT(final_dist >= combined_radius * 0.8f);
    ASSERT(final_dist < initial_dist + 1.0f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(topple_behavior)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);

    RngState rng;
    rng_seed(&rng, 12345);

    int32_t obj_idx = voxel_object_world_add_box(world, vec3_create(0.0f, 3.0f, 0.0f),
                                                 vec3_create(0.3f, 1.2f, 0.3f),
                                                 MAT_STONE, &rng);
    ASSERT(obj_idx >= 0);

    VoxelObject *obj = &world->objects[obj_idx];

    obj->orientation = quat_from_axis_angle(vec3_create(0.0f, 0.0f, 1.0f), 0.4f);
    obj->velocity.x = 1.5f;
    obj->bounds_dirty = true;

    float dt = 1.0f / 60.0f;
    Quat initial_orientation = obj->orientation;
    float max_rotation_change = 0.0f;

    for (int i = 0; i < 900; i++)
    {
        voxel_body_world_update(world, dt);

        if (!obj->active)
            break;

        float dot = obj->orientation.x * initial_orientation.x +
                    obj->orientation.y * initial_orientation.y +
                    obj->orientation.z * initial_orientation.z +
                    obj->orientation.w * initial_orientation.w;
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        float angle_change = 2.0f * acosf(fabsf(dot));
        if (angle_change > max_rotation_change)
            max_rotation_change = angle_change;

        if (obj->sleeping)
            break;
    }

    printf("(rot_change=%.2f, on_gnd=%d, sleep=%d, y=%.1f) ",
           max_rotation_change, obj->on_ground, obj->sleeping, obj->position.y);

    ASSERT(obj->active);
    ASSERT(obj->on_ground || obj->sleeping);
    ASSERT(max_rotation_change > 0.01f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(physics_profiling)
{
    profile_reset_all();

    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);
    world->enable_object_collision = true;

    RngState rng;
    rng_seed(&rng, 12345);

    for (int i = 0; i < 10; i++)
    {
        float x = (i % 5) * 2.0f - 4.0f;
        float z = (i / 5) * 2.0f - 1.0f;
        voxel_object_world_add_box(world, vec3_create(x, 20.0f + i * 3.0f, z),
                                   vec3_create(0.8f, 0.8f, 0.8f), MAT_STONE, &rng);
    }

    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; i++)
    {
        PROFILE_BEGIN(PROFILE_SIM_PHYSICS);
        voxel_body_world_update(world, dt);
        PROFILE_END(PROFILE_SIM_PHYSICS);
    }

    float physics_ms = profile_get_avg_ms(PROFILE_SIM_PHYSICS);
    float collision_ms = profile_get_avg_ms(PROFILE_SIM_COLLISION);
    int32_t physics_samples = profile_get_sample_count(PROFILE_SIM_PHYSICS);
    int32_t collision_samples = profile_get_sample_count(PROFILE_SIM_COLLISION);

    printf("(physics=%.3fms/%d, collision=%.3fms/%d) ",
           physics_ms, physics_samples, collision_ms, collision_samples);

    ASSERT(physics_samples == 120);
    ASSERT(collision_samples == 120);
    ASSERT(physics_ms > 0.0f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(performance_regression)
{
    Bounds3D bounds = {-32.0f, 32.0f, 0.0f, 64.0f, -32.0f, 32.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);
    world->enable_object_collision = true;

    RngState rng;
    rng_seed(&rng, 54321);

    for (int i = 0; i < 50; i++)
    {
        float x = ((i * 7) % 20) - 10.0f;
        float z = ((i * 11) % 20) - 10.0f;
        float y = 10.0f + (i % 10) * 5.0f;
        voxel_object_world_add_sphere(world, vec3_create(x, y, z), 0.8f, MAT_STONE, &rng);
    }

    float dt = 1.0f / 60.0f;
    uint64_t start = platform_get_ticks();

    for (int i = 0; i < 300; i++)
    {
        voxel_body_world_update(world, dt);
    }

    uint64_t end = platform_get_ticks();
    float elapsed_ms = (float)(end - start) * 1000.0f / (float)platform_get_frequency();
    float avg_frame_ms = elapsed_ms / 300.0f;

    printf("(total=%.1fms, avg=%.3fms/frame) ", elapsed_ms, avg_frame_ms);

    ASSERT(avg_frame_ms < 16.67f);
    ASSERT(elapsed_ms < 5000.0f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(center_of_mass_symmetric)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    RngState rng;
    rng_seed(&rng, 12345);

    int32_t idx = voxel_object_world_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                              vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE, &rng);
    ASSERT(idx >= 0);
    VoxelObject *obj = &world->objects[idx];

    printf("(com=%.3f,%.3f,%.3f) ", obj->center_of_mass_offset.x,
           obj->center_of_mass_offset.y, obj->center_of_mass_offset.z);

    ASSERT(fabsf(obj->center_of_mass_offset.x) < 0.01f);
    ASSERT(fabsf(obj->center_of_mass_offset.y) < 0.01f);
    ASSERT(fabsf(obj->center_of_mass_offset.z) < 0.01f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(bounding_sphere_accuracy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    RngState rng;
    rng_seed(&rng, 12345);

    int32_t idx = voxel_object_world_add_box(world, vec3_create(0.0f, 10.0f, 0.0f),
                                              vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE, &rng);
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

int main(void)
{
    platform_time_init();

    printf("=== Quaternion Math Tests ===\n");
    RUN_TEST(quat_identity_unit);
    RUN_TEST(quat_normalize_unit);
    RUN_TEST(quat_from_axis_angle_unit);
    RUN_TEST(quat_multiply_identity);
    RUN_TEST(quat_multiply_composition);
    RUN_TEST(quat_integrate_unit);
    RUN_TEST(quat_to_mat3_identity);
    RUN_TEST(quat_to_mat3_rotation);
    RUN_TEST(mat3_multiply_identity);
    RUN_TEST(mat3_transpose_unit);

    printf("\n=== Physics Tests ===\n");

    RUN_TEST(physics_state_init_destroy);
    RUN_TEST(proxy_alloc_free);
    RUN_TEST(proxy_free_list_reuse);
    RUN_TEST(proxy_gravity);
    RUN_TEST(proxy_floor_collision);
    RUN_TEST(proxy_sphere_collision);
    RUN_TEST(proxy_volume_collision);
    RUN_TEST(fragment_spawn);
    RUN_TEST(fragment_gravity);
    RUN_TEST(determinism);

    printf("\n=== Voxel Body Physics Tests ===\n");
    RUN_TEST(settling_time);
    RUN_TEST(collision_bounds_accuracy);
    RUN_TEST(topple_behavior);
    RUN_TEST(physics_profiling);
    RUN_TEST(performance_regression);
    RUN_TEST(center_of_mass_symmetric);
    RUN_TEST(bounding_sphere_accuracy);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
