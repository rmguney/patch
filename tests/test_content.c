#include "content/materials.h"
#include "content/scenes.h"
#include "content/voxel_shapes.h"
#include "test_common.h"
#include <math.h>
#include <string.h>

TEST(mat_air_is_zero)
{
    ASSERT(MAT_AIR == 0);
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
        ASSERT(material_get((uint8_t)i) != NULL);
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

TEST(scene_descriptors_valid)
{
    for (int i = 0; i < SCENE_TYPE_COUNT; i++)
    {
        const SceneDescriptor *desc = scene_get_descriptor((SceneType)i);
        ASSERT(desc != NULL);
        ASSERT(desc->name != NULL);
        ASSERT(desc->chunks_x > 0);
        ASSERT(desc->chunks_y > 0);
        ASSERT(desc->chunks_z > 0);
        ASSERT(desc->voxel_size > 0.0f);
    }
    return 1;
}

TEST(scene_bounds_match_chunks)
{
    /* Bounds and chunk counts are stored separately: they must describe the same box */
    for (int i = 0; i < SCENE_TYPE_COUNT; i++)
    {
        const SceneDescriptor *desc = scene_get_descriptor((SceneType)i);
        const float chunk_world = 32.0f * desc->voxel_size;
        const float eps = desc->voxel_size * 0.01f;
        ASSERT_NEAR(desc->bounds.max_x - desc->bounds.min_x, desc->chunks_x * chunk_world, eps);
        ASSERT_NEAR(desc->bounds.max_y - desc->bounds.min_y, desc->chunks_y * chunk_world, eps);
        ASSERT_NEAR(desc->bounds.max_z - desc->bounds.min_z, desc->chunks_z * chunk_world, eps);
    }
    return 1;
}

TEST(shape_table_valid)
{
    for (int i = 0; i < g_voxel_shape_count; i++)
    {
        const VoxelShape *shape = voxel_shape_get(i);
        ASSERT(shape != NULL);
        ASSERT(shape->voxels != NULL);
        ASSERT(shape->size_x > 0 && shape->size_y > 0 && shape->size_z > 0);
        /* Every voxel must be empty or a registered material */
        for (int v = 0; v < voxel_shape_total_voxels(shape); v++)
            ASSERT(shape->voxels[v] == MAT_AIR || shape->voxels[v] < g_material_count);
    }
    ASSERT(voxel_shape_get(g_voxel_shape_count) == NULL);
    return 1;
}

TEST(no_materials_past_count)
{
    /* A material registered above the highest counted ID would never reach the GPU palette */
    for (int i = g_material_count; i < MATERIAL_MAX_COUNT; i++)
        ASSERT(material_get((uint8_t)i) == NULL);
    return 1;
}

TEST(transparent_materials_are_liquid)
{
    /* The raymarcher shades every transparent voxel as water and ignores its colour, so a
     * transparent solid (say, ice) would render as open water */
    for (int i = 1; i < g_material_count; i++)
    {
        const MaterialDescriptor *mat = material_get((uint8_t)i);
        if (mat->flags & MAT_FLAG_TRANSPARENT)
            ASSERT((mat->flags & MAT_FLAG_LIQUID) != 0);
    }
    return 1;
}

TEST(liquids_hold_nothing)
{
    /* Bodies and debris sink through a liquid, and it never leaves with an island: a
     * liquid is never solid, nor breakable */
    for (int i = 1; i < g_material_count; i++)
    {
        const MaterialDescriptor *mat = material_get((uint8_t)i);
        if (mat->flags & MAT_FLAG_LIQUID)
            ASSERT((mat->flags & (MAT_FLAG_SOLID | MAT_FLAG_BREAKABLE)) == 0);
    }
    ASSERT(material_is_liquid(MAT_WATER));
    ASSERT(!material_is_solid(MAT_WATER));
    return 1;
}

TEST(ground_flora_is_not_solid)
{
    /* Flowers and mushrooms neither hold bodies nor debris up */
    static const uint8_t flora[] = {MAT_FLOWER_RED, MAT_FLOWER_BLUE, MAT_FLOWER_YELLOW, MAT_MUSHROOM,
                                    MAT_GLOWING_MUSHROOM};
    for (size_t i = 0; i < sizeof(flora) / sizeof(flora[0]); i++)
    {
        ASSERT(!material_is_solid(flora[i]));
        ASSERT(material_is_breakable(flora[i]));
    }
    ASSERT(material_is_solid(MAT_STONE) && material_is_solid(MAT_OAK_LEAF));
    return 1;
}

TEST(pastel_range_is_the_pastels)
{
    /* Props and pillars draw their colour from MAT_PASTEL_FIRST + [0, MAT_PASTEL_COUNT) */
    static const uint8_t pastels[] = {MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
                                      MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE};
    ASSERT_EQ(MAT_PASTEL_COUNT, (int)(sizeof(pastels) / sizeof(pastels[0])));
    for (int i = 0; i < MAT_PASTEL_COUNT; i++)
        ASSERT_EQ(MAT_PASTEL_FIRST + i, pastels[i]);
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
    RUN_TEST(scene_descriptors_valid);
    RUN_TEST(scene_bounds_match_chunks);
    RUN_TEST(shape_table_valid);
    RUN_TEST(no_materials_past_count);
    RUN_TEST(transparent_materials_are_liquid);
    RUN_TEST(liquids_hold_nothing);
    RUN_TEST(ground_flora_is_not_solid);
    RUN_TEST(pastel_range_is_the_pastels);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
