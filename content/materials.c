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
extern const MaterialDescriptor g_mat_green;
extern const MaterialDescriptor g_mat_red;
extern const MaterialDescriptor g_mat_cloud;
extern const MaterialDescriptor g_mat_rose;
extern const MaterialDescriptor g_mat_orange;
extern const MaterialDescriptor g_mat_glass;

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
    [MAT_GREEN] = &g_mat_green,
    [MAT_RED] = &g_mat_red,
    [MAT_CLOUD] = &g_mat_cloud,
    [MAT_ROSE] = &g_mat_rose,
    [MAT_ORANGE] = &g_mat_orange,
    [MAT_GLASS] = &g_mat_glass,
};

const int32_t g_material_count = 17;

static_assert(MAT_GLASS + 1 == 17, "Material count must match g_material_count");
static_assert(MAT_GLASS < MATERIAL_MAX_COUNT, "Material ID exceeds table size");
