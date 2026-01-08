#ifndef PATCH_CONTENT_VOXEL_SHAPES_H
#define PATCH_CONTENT_VOXEL_SHAPES_H

#include "engine/core/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * VoxelShape: immutable 3D voxel model descriptor.
 *
 * Generated at build time from mesh files using tools/voxelize.
 * Stored as flat array of material IDs (0 = empty).
 * Used for entity visuals, items, projectiles, etc.
 *
 * Layout: voxels[x + y * size_x + z * size_x * size_y]
 * Origin: (0,0,0) is min corner of bounding box.
 */
typedef struct
{
    const char *name;           /* Identifier for debug/tools */
    int32_t size_x;             /* Dimensions in voxels */
    int32_t size_y;
    int32_t size_z;
    const uint8_t *voxels;      /* Flat array [size_x * size_y * size_z] */
    int32_t solid_count;        /* Number of non-empty voxels */
    float center_of_mass_x;     /* Precomputed center of mass */
    float center_of_mass_y;
    float center_of_mass_z;
} VoxelShape;

/* Compile-time layout validation */
_Static_assert(sizeof(VoxelShape) == 48 || sizeof(VoxelShape) == 56,
               "VoxelShape size changed - verify cross-platform layout");

/* Maximum number of registered shapes */
#define VOXEL_SHAPE_MAX_COUNT 64

/*
 * Global shape table - explicit registration.
 * Defined in voxel_shapes.c, declared here for engine access.
 */
extern const VoxelShape *const g_voxel_shapes[VOXEL_SHAPE_MAX_COUNT];
extern const int32_t g_voxel_shape_count;

/*
 * Lookup shape by index.
 * Returns NULL if index out of range.
 */
static inline const VoxelShape *voxel_shape_get(int32_t index)
{
    if (index < 0 || index >= g_voxel_shape_count)
        return NULL;
    return g_voxel_shapes[index];
}

/*
 * Get voxel material at local coordinates.
 * Returns 0 (empty) if out of bounds.
 */
static inline uint8_t voxel_shape_get_at(const VoxelShape *shape, int32_t x, int32_t y, int32_t z)
{
    if (!shape || x < 0 || x >= shape->size_x ||
        y < 0 || y >= shape->size_y ||
        z < 0 || z >= shape->size_z)
    {
        return 0;
    }
    return shape->voxels[x + y * shape->size_x + z * shape->size_x * shape->size_y];
}

/*
 * Get total voxel count (including empty).
 */
static inline int32_t voxel_shape_total_voxels(const VoxelShape *shape)
{
    if (!shape)
        return 0;
    return shape->size_x * shape->size_y * shape->size_z;
}

/*
 * Shape IDs - must match registration order in voxel_shapes.c.
 * Add new shapes here as they are created.
 */
#define SHAPE_CUBE 0
#define SHAPE_SPHERE 1
#define SHAPE_SWORD 2
#define SHAPE_AXE 3

#ifdef __cplusplus
}
#endif

#endif /* PATCH_CONTENT_VOXEL_SHAPES_H */
