#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/voxel_object.h"
#include "engine/sim/detach.h"
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

int main(void)
{
    platform_time_init();

    printf("=== Matrix Math Tests ===\n");
    RUN_TEST(mat3_multiply_identity);
    RUN_TEST(mat3_transpose_unit);

    printf("\n=== Voxel Object Shape Tests ===\n");
    RUN_TEST(shape_half_extents_symmetric);
    RUN_TEST(bounding_sphere_accuracy);

    printf("\n=== Object Fragmentation Tests ===\n");
    RUN_TEST(object_split_creates_fragments);
    RUN_TEST(object_split_dumbbell);

    printf("\n=== Object Raycast Tests ===\n");
    RUN_TEST(object_raycast_hit);
    RUN_TEST(object_raycast_miss);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
