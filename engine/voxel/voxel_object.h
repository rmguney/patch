#ifndef PATCH_VOXEL_OBJECT_H
#define PATCH_VOXEL_OBJECT_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/spatial_hash.h"
#include "engine/voxel/volume.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct BVH BVH;

#ifdef __cplusplus
extern "C"
{
#endif

#define VOBJ_GRID_SIZE 32
#define VOBJ_TOTAL_VOXELS (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE)
#define VOBJ_MAX_OBJECTS 512
#define VOBJ_MAX_SURFACE_VOXELS 512
#define VOBJ_MAX_COLLIDER_BOXES 48

    typedef struct
    {
        Vec3 local_min;
        Vec3 local_max;
    } ColliderBox;

#define VOBJ_RAYCAST_CELL_SIZE 25.0f
#define VOBJ_RAYCAST_QUERY_RADIUS 50.0f
#define VOBJ_RAYCAST_MAX_DIST 500.0f
#define VOBJ_RAYCAST_STEP_MULT 1.5f
#define VOBJ_RAYCAST_MAX_CANDIDATES 256
#define VOBJ_RAYCAST_PER_QUERY_MAX 64
#define VOBJ_DDA_MAX_STEPS (VOBJ_GRID_SIZE * 6)
#define VOBJ_DIR_EPSILON 0.0001f
#define VOBJ_SPHERE_ENTRY_BIAS 0.2f

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
        uint32_t voxel_revision;

        float radius;
        Vec3 shape_half_extents;
        Vec3 local_com;          /* Center of mass offset from grid center (local space) */
        float total_mass;        /* Mass from per-material density */
        Vec3 inertia_diag;       /* Diagonal inertia tensor about COM */

        Vec3 surface_voxels[VOBJ_MAX_SURFACE_VOXELS];
        int32_t surface_voxel_count;

        ColliderBox collider_boxes[VOBJ_MAX_COLLIDER_BOXES];
        int32_t collider_box_count;

        bool active;
        bool shape_dirty;       /* Deferred recalc flag */
        int32_t render_delay;   /* Frames to skip rendering (terrain GPU sync) */
        uint8_t occupancy_mask; /* 8 regions of 8³ voxels each */
        int32_t next_free;      /* Free-list chain (-1 = end or not free) */
        int32_t next_dirty;     /* Dirty-list chain (-1 = end or not dirty) */
    } VoxelObject;

#define VOBJ_SPLIT_QUEUE_SIZE 256
#define VOBJ_MAX_SPLITS_PER_TICK 4
#define VOBJ_MAX_RECALCS_PER_TICK 8

    typedef struct VoxelObjectWorld
    {
        VoxelObject objects[VOBJ_MAX_OBJECTS];
        int32_t object_count;

        Bounds3D bounds;
        float voxel_size;

        VoxelVolume *terrain;

        /* Free-list for O(1) allocation */
        int32_t first_free_slot;

        /* Dirty-list for O(1) recalc lookup */
        int32_t first_dirty;
        int32_t dirty_count;

        /* Deferred split work queue */
        int32_t split_queue[VOBJ_SPLIT_QUEUE_SIZE];
        int32_t split_queue_head;
        int32_t split_queue_tail;

        /* Spatial hash for raycast acceleration (legacy) */
        SpatialHashGrid *raycast_grid;
        bool raycast_grid_valid;

        /* BVH for accelerated object queries */
        BVH *bvh;
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

    typedef struct
    {
        bool hit;
        int32_t object_index;
        Vec3 surface_normal;
    } VoxelObjectPointTest;

    VoxelObjectPointTest voxel_object_world_test_point(const VoxelObjectWorld *world, Vec3 world_pos);

    void voxel_object_recalc_shape(VoxelObject *obj);
    void voxel_object_mark_dirty(VoxelObject *obj);
    void voxel_object_world_mark_dirty(VoxelObjectWorld *world, int32_t obj_index);
    void voxel_object_world_free_slot(VoxelObjectWorld *world, int32_t slot);

    int32_t voxel_object_world_alloc_slot(VoxelObjectWorld *world);

    /* Per-frame deferred processing */
    void voxel_object_world_process_splits(VoxelObjectWorld *world);
    void voxel_object_world_process_recalcs(VoxelObjectWorld *world);
    void voxel_object_world_tick_render_delays(VoxelObjectWorld *world);
    void voxel_object_world_queue_split(VoxelObjectWorld *world, int32_t obj_index);

    /* Raycast acceleration */
    void voxel_object_world_update_raycast_grid(VoxelObjectWorld *world);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_OBJECT_H */
