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
 * 6. UPDATE: Increment g_material_count and _Static_assert.
 *
 * === LINK-TIME VALIDATION ===
 *
 * - Missing material file → linker error (unresolved symbol)
 * - Missing registration → undefined material (NULL pointer)
 * - ID mismatch → _Static_assert fails
 */

#include "materials.h"

/* Extern declarations for materials defined in content/materials/ */
extern const MaterialDescriptor g_mat_air;
extern const MaterialDescriptor g_mat_stone;
extern const MaterialDescriptor g_mat_dirt;
extern const MaterialDescriptor g_mat_grass;
extern const MaterialDescriptor g_mat_sand;
extern const MaterialDescriptor g_mat_wood;
extern const MaterialDescriptor g_mat_brick;
extern const MaterialDescriptor g_mat_concrete;
extern const MaterialDescriptor g_mat_metal;
extern const MaterialDescriptor g_mat_glass;
extern const MaterialDescriptor g_mat_water;
extern const MaterialDescriptor g_mat_flesh;
extern const MaterialDescriptor g_mat_bone;
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
extern const MaterialDescriptor g_mat_white;
extern const MaterialDescriptor g_mat_yellow;
extern const MaterialDescriptor g_mat_glow;
extern const MaterialDescriptor g_mat_chrome;
extern const MaterialDescriptor g_mat_red;
extern const MaterialDescriptor g_mat_green;

/*
 * Global material registration table.
 * Uses designated initializers to match MAT_* constants.
 */
const MaterialDescriptor *const g_materials[MATERIAL_MAX_COUNT] = {
    [MAT_AIR] = &g_mat_air,
    [MAT_STONE] = &g_mat_stone,
    [MAT_DIRT] = &g_mat_dirt,
    [MAT_GRASS] = &g_mat_grass,
    [MAT_SAND] = &g_mat_sand,
    [MAT_WOOD] = &g_mat_wood,
    [MAT_BRICK] = &g_mat_brick,
    [MAT_CONCRETE] = &g_mat_concrete,
    [MAT_METAL] = &g_mat_metal,
    [MAT_GLASS] = &g_mat_glass,
    [MAT_WATER] = &g_mat_water,
    [MAT_FLESH] = &g_mat_flesh,
    [MAT_BONE] = &g_mat_bone,
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
    [MAT_WHITE] = &g_mat_white,
    [MAT_YELLOW] = &g_mat_yellow,
    [MAT_GLOW] = &g_mat_glow,
    [MAT_CHROME] = &g_mat_chrome,
    [MAT_RED] = &g_mat_red,
    [MAT_GREEN] = &g_mat_green,
};

const int32_t g_material_count = 30;

_Static_assert(MAT_GREEN + 1 == 30, "Material count must match g_material_count");
_Static_assert(MAT_GREEN < MATERIAL_MAX_COUNT, "Material ID exceeds table size");
