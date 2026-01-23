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
 * 5. UPDATE: Increment g_voxel_shape_count and static_assert.
 *
 * === GENERATED SHAPES (from voxelize tool) ===
 *
 * Run: ./build/voxelize models/helmet.obj content/shapes/shape_helmet.c --name helmet
 * Then follow steps 2-5 above.
 *
 * === LINK-TIME VALIDATION ===
 *
 * - Missing shape file → linker error (unresolved symbol)
 * - Missing registration → static_assert fails
 * - ID mismatch → static_assert fails
 */

#include "voxel_shapes.h"

/* Extern declarations for shapes defined in content/shapes/ */
extern const VoxelShape g_shape_sphere;
extern const VoxelShape g_shape_cube;
extern const VoxelShape g_shape_gary;

/*
 * Global shape registration table.
 * Order must match SHAPE_* constants in voxel_shapes.h.
 */
const VoxelShape *const g_voxel_shapes[VOXEL_SHAPE_MAX_COUNT] = {
    &g_shape_sphere, /* SHAPE_SPHERE = 0 */
    &g_shape_cube,   /* shape_cube = 1 */
    &g_shape_gary,   /* SHAPE_GARY = 2 */
};

const int32_t g_voxel_shape_count = 3;

static_assert(SHAPE_GARY + 1 == 3, "Shape count must match g_voxel_shape_count");
