#include "content/materials.h"
#include "content/scenes.h"
#include <stdio.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  %s... ", #name); \
    if (test_##name()) { g_tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { printf("ASSERT FAILED: %s\n", #cond); return 0; } } while(0)

TEST(mat_air_is_zero)
{
    ASSERT(MAT_AIR == 0);
    ASSERT(MATERIAL_ID_EMPTY == 0);
    return 1;
}

TEST(material_count_valid)
{
    ASSERT(g_material_count > 0);
    ASSERT(g_material_count <= MATERIAL_MAX_COUNT);
    return 1;
}

TEST(material_table_not_null)
{
    for (int i = 0; i < g_material_count; i++)
    {
        const MaterialDescriptor *mat = material_get((uint8_t)i);
        ASSERT(mat != NULL);
        ASSERT(mat->name != NULL);
    }
    return 1;
}

TEST(air_is_transparent)
{
    const MaterialDescriptor *air = material_get(MAT_AIR);
    ASSERT((air->flags & MAT_FLAG_TRANSPARENT) != 0);
    ASSERT((air->flags & MAT_FLAG_SOLID) == 0);
    return 1;
}

TEST(solid_materials_have_density)
{
    for (int i = 1; i < g_material_count; i++)
    {
        const MaterialDescriptor *mat = material_get((uint8_t)i);
        if (mat->flags & MAT_FLAG_SOLID)
        {
            ASSERT(mat->density > 0.0f);
        }
    }
    return 1;
}

TEST(material_color_valid)
{
    for (int i = 0; i < g_material_count; i++)
    {
        Vec3 color = material_get_color((uint8_t)i);
        ASSERT(color.x >= 0.0f && color.x <= 1.0f);
        ASSERT(color.y >= 0.0f && color.y <= 1.0f);
        ASSERT(color.z >= 0.0f && color.z <= 1.0f);
    }
    return 1;
}

TEST(scene_count_valid)
{
    ASSERT(g_scene_count > 0);
    ASSERT(g_scene_count <= SCENE_MAX_COUNT);
    return 1;
}

TEST(scene_descriptors_valid)
{
    for (int i = 0; i < g_scene_count; i++)
    {
        const SceneDescriptor *desc = scene_get_descriptor(i);
        ASSERT(desc != NULL);
        ASSERT(desc->name != NULL);
        ASSERT(desc->chunks_x > 0);
        ASSERT(desc->chunks_y > 0);
        ASSERT(desc->chunks_z > 0);
        ASSERT(desc->voxel_size > 0.0f);
    }
    return 1;
}

TEST(scene_bounds_valid)
{
    for (int i = 0; i < g_scene_count; i++)
    {
        const SceneDescriptor *desc = scene_get_descriptor(i);
        ASSERT(desc->bounds.max_x > desc->bounds.min_x);
        ASSERT(desc->bounds.max_y > desc->bounds.min_y);
        ASSERT(desc->bounds.max_z > desc->bounds.min_z);
    }
    return 1;
}

TEST(predefined_materials_in_range)
{
    ASSERT(MAT_STONE < g_material_count);
    ASSERT(MAT_DIRT < g_material_count);
    ASSERT(MAT_GRASS < g_material_count);
    ASSERT(MAT_WOOD < g_material_count);
    ASSERT(MAT_BRICK < g_material_count);
    ASSERT(MAT_CONCRETE < g_material_count);
    ASSERT(MAT_METAL < g_material_count);
    ASSERT(MAT_PINK < g_material_count);
    ASSERT(MAT_ROSE < g_material_count);
    return 1;
}

int main(void)
{
    printf("=== Content Sanity Tests ===\n");

    RUN_TEST(mat_air_is_zero);
    RUN_TEST(material_count_valid);
    RUN_TEST(material_table_not_null);
    RUN_TEST(air_is_transparent);
    RUN_TEST(solid_materials_have_density);
    RUN_TEST(material_color_valid);
    RUN_TEST(scene_count_valid);
    RUN_TEST(scene_descriptors_valid);
    RUN_TEST(scene_bounds_valid);
    RUN_TEST(predefined_materials_in_range);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
