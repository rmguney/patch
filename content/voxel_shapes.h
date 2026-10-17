#ifndef PATCH_CONTENT_VOXEL_SHAPES_H
#define PATCH_CONTENT_VOXEL_SHAPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * VoxelShape: immutable 3D voxel model descriptor, spawned as voxel objects.
     *
     * Hand-authored or converted from OBJ meshes with tools/voxelize.
     * Stored as flat array of material IDs (0 = empty).
     *
     * Layout: voxels[x + y * size_x + z * size_x * size_y]
     * Origin: (0,0,0) is min corner of bounding box.
     */
    typedef struct
    {
        int32_t size_x; /* Dimensions in voxels */
        int32_t size_y;
        int32_t size_z;
        const uint8_t *voxels; /* Flat array [size_x * size_y * size_z] */
    } VoxelShape;

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
     * Get total voxel count (including empty).
     */
    static inline int32_t voxel_shape_total_voxels(const VoxelShape *shape)
    {
        if (!shape)
            return 0;
        return shape->size_x * shape->size_y * shape->size_z;
    }

/* Shape IDs: indices into g_voxel_shapes (registered in voxel_shapes.c) */
#define SHAPE_SPHERE 0
#define SHAPE_CUBE 1
#define SHAPE_GARY 2

#ifdef __cplusplus
}
#endif

#endif /* PATCH_CONTENT_VOXEL_SHAPES_H */
