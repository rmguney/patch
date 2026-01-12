#ifndef PATCH_VOXEL_OBJECT_H
#define PATCH_VOXEL_OBJECT_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define VOBJ_GRID_SIZE 16
#define VOBJ_TOTAL_VOXELS (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE)
#define VOBJ_MAX_OBJECTS 4096

    typedef struct
    {
        uint8_t material;
    } VObjVoxel;

    typedef struct VoxelObject
    {
        Vec3 position;
        Quat orientation;

        VObjVoxel voxels[VOBJ_TOTAL_VOXELS];
        float voxel_size;
        int32_t voxel_count;

        float radius;
        Vec3 shape_half_extents;

        bool active;
        bool shape_dirty;        /* Deferred recalc flag */
        uint8_t occupancy_mask;  /* 8 regions of 8³ voxels each */
    } VoxelObject;

#define VOBJ_SPLIT_QUEUE_SIZE 64
#define VOBJ_MAX_SPLITS_PER_TICK 4
#define VOBJ_MAX_RECALCS_PER_TICK 8

    typedef struct
    {
        VoxelObject objects[VOBJ_MAX_OBJECTS];
        int32_t object_count;

        Bounds3D bounds;
        float voxel_size;

        VoxelVolume *terrain;

        /* Deferred split work queue */
        int32_t split_queue[VOBJ_SPLIT_QUEUE_SIZE];
        int32_t split_queue_head;
        int32_t split_queue_tail;
    } VoxelObjectWorld;

    typedef struct
    {
        bool hit;
        int32_t object_index;
        Vec3 impact_point;
        Vec3 impact_normal;
        Vec3 impact_normal_local;
        int32_t voxel_x, voxel_y, voxel_z;
    } VoxelObjectHit;

    static inline int32_t vobj_index(int32_t x, int32_t y, int32_t z)
    {
        return x + y * VOBJ_GRID_SIZE + z * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE;
    }

    static inline void vobj_coords(int32_t idx, int32_t *x, int32_t *y, int32_t *z)
    {
        *x = idx % VOBJ_GRID_SIZE;
        *y = (idx / VOBJ_GRID_SIZE) % VOBJ_GRID_SIZE;
        *z = idx / (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE);
    }

    VoxelObjectWorld *voxel_object_world_create(Bounds3D bounds, float voxel_size);
    void voxel_object_world_destroy(VoxelObjectWorld *world);

    void voxel_object_world_set_terrain(VoxelObjectWorld *world, VoxelVolume *terrain);

    int32_t voxel_object_world_add_sphere(VoxelObjectWorld *world, Vec3 position,
                                          float radius, uint8_t material);
    int32_t voxel_object_world_add_box(VoxelObjectWorld *world, Vec3 position,
                                       Vec3 half_extents, uint8_t material);
    int32_t voxel_object_world_add_from_voxels(VoxelObjectWorld *world,
                                               const uint8_t *voxels,
                                               int32_t size_x, int32_t size_y, int32_t size_z,
                                               Vec3 origin, float voxel_size);

    VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld *world, Vec3 origin, Vec3 dir);

    void voxel_object_recalc_shape(VoxelObject *obj);
    void voxel_object_mark_dirty(VoxelObject *obj);

    int32_t voxel_object_world_alloc_slot(VoxelObjectWorld *world);

    /* Per-frame deferred processing */
    void voxel_object_world_process_splits(VoxelObjectWorld *world);
    void voxel_object_world_process_recalcs(VoxelObjectWorld *world);
    void voxel_object_world_queue_split(VoxelObjectWorld *world, int32_t obj_index);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_OBJECT_H */
