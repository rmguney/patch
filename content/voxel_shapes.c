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
 *    or convert a mesh: voxelize models/helmet.obj content/shapes/shape_helmet.c --name helmet
 *
 * 2. BUILD: The CMake glob over content/shapes/ picks the file up.
 *
 * 3. DECLARE: In voxel_shapes.h, add:
 *    #define SHAPE_<NAME> N   // next available ID
 *
 * 4. REGISTER: Add an extern below and a pointer to g_voxel_shapes[] at index N.
 *
 * 5. UPDATE: Base g_voxel_shape_count on the new highest ID.
 *
 * - Missing shape file → linker error (unresolved symbol)
 * - Missing registration → NULL table entry (test_content checks every ID)
 */

#include "voxel_shapes.h"

/* Extern declarations for shapes defined in content/shapes/ */
extern const VoxelShape g_shape_sphere;
extern const VoxelShape g_shape_cube;
extern const VoxelShape g_shape_gary;

/* Global shape registration table, indexed by SHAPE_* */
const VoxelShape *const g_voxel_shapes[VOXEL_SHAPE_MAX_COUNT] = {
    [SHAPE_SPHERE] = &g_shape_sphere,
    [SHAPE_CUBE] = &g_shape_cube,
    [SHAPE_GARY] = &g_shape_gary,
};

/* SHAPE_GARY is the highest registered ID */
const int32_t g_voxel_shape_count = SHAPE_GARY + 1;
