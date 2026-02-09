/*
 * materials.c - Material Registration Table
 *
 * Central registration for all materials.
 * Individual material descriptors are defined in content/materials/ (one file per material).
 *
 * === ADDING A NEW MATERIAL ===
 *
 * 1. CREATE FILE: Add content/materials/mat_<name>.c with:
 *    const MaterialDescriptor g_mat_<name> = { ... };
 *
 * 2. ADD TO BUILD: Update CMakeLists.txt CONTENT_MATERIAL_SOURCES or rely on glob.
 *
 * 3. DECLARE: In materials.h, add:
 *    #define MAT_<NAME> N   // next available ID
 *
 * 4. EXTERN: Add extern declaration below.
 *
 * 5. REGISTER: Add pointer to g_materials[] using designated initializer.
 *
 * 6. UPDATE: Increment g_material_count and static_assert.
 *
 * === LINK-TIME VALIDATION ===
 *
 * - Missing material file → linker error (unresolved symbol)
 * - Missing registration → undefined material (NULL pointer)
 * - ID mismatch → static_assert fails
 */

#include "materials.h"

/* Extern declarations for materials defined in content/materials/ */
extern const MaterialDescriptor g_mat_air;
extern const MaterialDescriptor g_mat_stone;
extern const MaterialDescriptor g_mat_dirt;
extern const MaterialDescriptor g_mat_grass;
extern const MaterialDescriptor g_mat_metal;
extern const MaterialDescriptor g_mat_pink;
extern const MaterialDescriptor g_mat_cyan;
extern const MaterialDescriptor g_mat_peach;
extern const MaterialDescriptor g_mat_mint;
extern const MaterialDescriptor g_mat_lavender;
extern const MaterialDescriptor g_mat_sky;
extern const MaterialDescriptor g_mat_teal;
extern const MaterialDescriptor g_mat_coral;
extern const MaterialDescriptor g_mat_cloud;
extern const MaterialDescriptor g_mat_rose;
extern const MaterialDescriptor g_mat_orange;
extern const MaterialDescriptor g_mat_oak_bark;
extern const MaterialDescriptor g_mat_pine_bark;
extern const MaterialDescriptor g_mat_birch_bark;
extern const MaterialDescriptor g_mat_jungle_bark;
extern const MaterialDescriptor g_mat_oak_leaf;
extern const MaterialDescriptor g_mat_pine_leaf;
extern const MaterialDescriptor g_mat_birch_leaf;
extern const MaterialDescriptor g_mat_jungle_leaf;
extern const MaterialDescriptor g_mat_cherry_leaf;
extern const MaterialDescriptor g_mat_autumn_leaf;
extern const MaterialDescriptor g_mat_dead_wood;
extern const MaterialDescriptor g_mat_flower_red;
extern const MaterialDescriptor g_mat_flower_blue;
extern const MaterialDescriptor g_mat_flower_yellow;
extern const MaterialDescriptor g_mat_mushroom;
extern const MaterialDescriptor g_mat_sand;
extern const MaterialDescriptor g_mat_snow;
extern const MaterialDescriptor g_mat_ice;
extern const MaterialDescriptor g_mat_moss;
extern const MaterialDescriptor g_mat_clay;
extern const MaterialDescriptor g_mat_gravel;
extern const MaterialDescriptor g_mat_cobblestone;
extern const MaterialDescriptor g_mat_weak_rock;
extern const MaterialDescriptor g_mat_glowing_rock;
extern const MaterialDescriptor g_mat_glowing_mushroom;
extern const MaterialDescriptor g_mat_water;

/*
 * Global material registration table.
 * Uses designated initializers to match MAT_* constants.
 */
const MaterialDescriptor *const g_materials[MATERIAL_MAX_COUNT] = {
    [MAT_AIR] = &g_mat_air,
    [MAT_STONE] = &g_mat_stone,
    [MAT_DIRT] = &g_mat_dirt,
    [MAT_GRASS] = &g_mat_grass,
    [MAT_METAL] = &g_mat_metal,
    [MAT_PINK] = &g_mat_pink,
    [MAT_CYAN] = &g_mat_cyan,
    [MAT_PEACH] = &g_mat_peach,
    [MAT_MINT] = &g_mat_mint,
    [MAT_LAVENDER] = &g_mat_lavender,
    [MAT_SKY] = &g_mat_sky,
    [MAT_TEAL] = &g_mat_teal,
    [MAT_CORAL] = &g_mat_coral,
    [MAT_CLOUD] = &g_mat_cloud,
    [MAT_ROSE] = &g_mat_rose,
    [MAT_ORANGE] = &g_mat_orange,
    [MAT_OAK_BARK] = &g_mat_oak_bark,
    [MAT_PINE_BARK] = &g_mat_pine_bark,
    [MAT_BIRCH_BARK] = &g_mat_birch_bark,
    [MAT_JUNGLE_BARK] = &g_mat_jungle_bark,
    [MAT_OAK_LEAF] = &g_mat_oak_leaf,
    [MAT_PINE_LEAF] = &g_mat_pine_leaf,
    [MAT_BIRCH_LEAF] = &g_mat_birch_leaf,
    [MAT_JUNGLE_LEAF] = &g_mat_jungle_leaf,
    [MAT_CHERRY_LEAF] = &g_mat_cherry_leaf,
    [MAT_AUTUMN_LEAF] = &g_mat_autumn_leaf,
    [MAT_DEAD_WOOD] = &g_mat_dead_wood,
    [MAT_FLOWER_RED] = &g_mat_flower_red,
    [MAT_FLOWER_BLUE] = &g_mat_flower_blue,
    [MAT_FLOWER_YELLOW] = &g_mat_flower_yellow,
    [MAT_MUSHROOM] = &g_mat_mushroom,
    [MAT_SAND] = &g_mat_sand,
    [MAT_SNOW] = &g_mat_snow,
    [MAT_ICE] = &g_mat_ice,
    [MAT_MOSS] = &g_mat_moss,
    [MAT_CLAY] = &g_mat_clay,
    [MAT_GRAVEL] = &g_mat_gravel,
    [MAT_COBBLESTONE] = &g_mat_cobblestone,
    [MAT_WEAK_ROCK] = &g_mat_weak_rock,
    [MAT_GLOWING_ROCK] = &g_mat_glowing_rock,
    [MAT_GLOWING_MUSHROOM] = &g_mat_glowing_mushroom,
    [MAT_WATER] = &g_mat_water,
};

const int32_t g_material_count = 42;

STATIC_ASSERT(MAT_WATER + 1 == 42, "Material count must match g_material_count");
STATIC_ASSERT(MAT_WATER < MATERIAL_MAX_COUNT, "Material ID exceeds table size");
