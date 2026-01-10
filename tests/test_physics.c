#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/sim/voxel_object.h"
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

static int32_t create_dumbbell_object(VoxelObjectWorld *world, Vec3 position, uint8_t material)
{
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;

    int32_t slot = world->object_count++;
    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->velocity = vec3_zero();
    obj->orientation = quat_identity();
    obj->angular_velocity = vec3_zero();
    obj->active = true;
    obj->bounds_dirty = true;
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

    obj->mass = (float)obj->voxel_count * 1.0f;
    if (obj->mass > 0.0f)
        obj->inv_mass = 1.0f / obj->mass;

    Vec3 com = vec3_zero();
    Vec3 min_v = vec3_create(1e10f, 1e10f, 1e10f);
    Vec3 max_v = vec3_create(-1e10f, -1e10f, -1e10f);

    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material != 0)
        {
            int32_t vx, vy, vz;
            vobj_coords(i, &vx, &vy, &vz);
            float wx = ((float)vx - half_grid + 0.5f) * obj->voxel_size;
            float wy = ((float)vy - half_grid + 0.5f) * obj->voxel_size;
            float wz = ((float)vz - half_grid + 0.5f) * obj->voxel_size;
            com = vec3_add(com, vec3_create(wx, wy, wz));
            if (wx < min_v.x) min_v.x = wx;
            if (wy < min_v.y) min_v.y = wy;
            if (wz < min_v.z) min_v.z = wz;
            if (wx > max_v.x) max_v.x = wx;
            if (wy > max_v.y) max_v.y = wy;
            if (wz > max_v.z) max_v.z = wz;
        }
    }
    if (obj->voxel_count > 0)
        com = vec3_scale(com, 1.0f / (float)obj->voxel_count);

    obj->center_of_mass_offset = com;
    obj->shape_half_extents = vec3_scale(vec3_sub(max_v, min_v), 0.5f);
    obj->radius = vec3_length(obj->shape_half_extents);

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

    int32_t destroyed = voxel_object_destroy_at_point(world, obj_idx, bridge_point, 0.5f,
                                                       destroyed_pos, destroyed_mat, 64);

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

TEST(object_split_separation_impulse)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    int32_t obj_idx = create_dumbbell_object(world, vec3_create(0.0f, 20.0f, 0.0f), MAT_STONE);
    ASSERT(obj_idx >= 0);

    Vec3 bridge_point = vec3_create(0.0f, 20.0f, 0.0f);
    voxel_object_destroy_at_point(world, obj_idx, bridge_point, 0.5f, NULL, NULL, 0);

    ASSERT(world->object_count >= 2);

    Vec3 vel0 = world->objects[0].velocity;
    Vec3 vel1 = world->objects[1].velocity;

    Vec3 vel_diff = vec3_sub(vel0, vel1);
    float vel_diff_mag = vec3_length(vel_diff);

    printf("(vel_diff=%.3f) ", vel_diff_mag);

    ASSERT(vel_diff_mag > 0.1f);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(object_raycast_hit)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 64.0f, -16.0f, 16.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.25f);
    ASSERT(world != NULL);

    RngState rng;
    rng_seed(&rng, 12345);

    int32_t obj_idx = voxel_object_world_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                                                  vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE, &rng);
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

    RngState rng;
    rng_seed(&rng, 12345);

    voxel_object_world_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                               vec3_create(1.0f, 1.0f, 1.0f), MAT_STONE, &rng);

    Vec3 origin = vec3_create(10.0f, 20.0f, 0.0f);
    Vec3 dir = vec3_create(0.0f, 1.0f, 0.0f);

    VoxelObjectHit hit = voxel_object_world_raycast(world, origin, dir);

    ASSERT(hit.hit == false);

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

    printf("\n=== Voxel Object Shape Tests ===\n");
    RUN_TEST(center_of_mass_symmetric);
    RUN_TEST(bounding_sphere_accuracy);

    printf("\n=== Object Fragmentation Tests ===\n");
    RUN_TEST(object_split_creates_fragments);
    RUN_TEST(object_split_separation_impulse);

    printf("\n=== Object Raycast Tests ===\n");
    RUN_TEST(object_raycast_hit);
    RUN_TEST(object_raycast_miss);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
