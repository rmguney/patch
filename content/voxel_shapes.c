/*
 * voxel_shapes.c - Shape Registration Table
 *
 * Central registration for all voxel shapes.
 * Individual shape descriptors are defined in content/shapes/ (one file per shape).
 *
 * === ADDING A NEW SHAPE ===
 *
 * 1. CREATE FILE: Add content/shapes/shape_<name>.c with:
 *    - static const uint8_t k_<name>_voxels[...]
 *    - const VoxelShape g_shape_<name> = { ... }
 *
 * 2. ADD TO BUILD: Update CMakeLists.txt CONTENT_SHAPE_SOURCES or rely on glob.
 *
 * 3. DECLARE: In voxel_shapes.h, add:
 *    #define SHAPE_<NAME> N   // next available ID
 *    extern const VoxelShape g_shape_<name>;
 *
 * 4. REGISTER: Add pointer to g_voxel_shapes[] below.
 *
 * 5. UPDATE: Increment g_voxel_shape_count and _Static_assert.
 *
 * === GENERATED SHAPES (from voxelize tool) ===
 *
 * Run: ./build/voxelize models/helmet.obj content/shapes/shape_helmet.c --name helmet
 * Then follow steps 2-5 above.
 *
 * === LINK-TIME VALIDATION ===
 *
 * - Missing shape file → linker error (unresolved symbol)
 * - Missing registration → _Static_assert fails
 * - ID mismatch → _Static_assert fails
 */

#include "voxel_shapes.h"

/* Extern declarations for shapes defined in content/shapes/ */
extern const VoxelShape g_shape_cube;
extern const VoxelShape g_shape_sphere;
extern const VoxelShape g_shape_sword;
extern const VoxelShape g_shape_axe;

/*
 * Global shape registration table.
 * Order must match SHAPE_* constants in voxel_shapes.h.
 */
const VoxelShape *const g_voxel_shapes[VOXEL_SHAPE_MAX_COUNT] = {
    &g_shape_cube,   /* SHAPE_CUBE = 0 */
    &g_shape_sphere, /* SHAPE_SPHERE = 1 */
    &g_shape_sword,  /* SHAPE_SWORD = 2 */
    &g_shape_axe,    /* SHAPE_AXE = 3 */
};

const int32_t g_voxel_shape_count = 4;

_Static_assert(SHAPE_AXE + 1 == 4, "Shape count must match g_voxel_shape_count");
