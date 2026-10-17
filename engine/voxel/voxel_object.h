#ifndef PATCH_VOXEL_OBJECT_H
#define PATCH_VOXEL_OBJECT_H

#include "engine/core/types.h"
#include "engine/core/math.h"
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

/* The grid in 8³ bricks, tiled from each object's brick_offset: the unit of change tracking
 * (brick_revision) and of the GPU atlas */
#define VOBJ_BRICK_SIZE 8
#define VOBJ_BRICKS_PER_AXIS (VOBJ_GRID_SIZE / VOBJ_BRICK_SIZE)
#define VOBJ_BRICK_COUNT (VOBJ_BRICKS_PER_AXIS * VOBJ_BRICKS_PER_AXIS * VOBJ_BRICKS_PER_AXIS)
#define VOBJ_ALL_BRICKS UINT64_MAX

#define VOBJ_RAYCAST_MAX_DIST 500.0f
#define VOBJ_RAYCAST_MAX_CANDIDATES 256
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
        /* The pose at the start of the last simulation tick (voxel_object_world_begin_tick):
         * drawing blends from it to the current one (voxel_object_draw_pose) */
        Vec3 prev_position;
        Quat prev_orientation;

        VObjVoxel voxels[VOBJ_TOTAL_VOXELS];
        float voxel_size;
        int32_t voxel_count;
        uint32_t voxel_revision;

        float radius;
        Vec3 shape_half_extents; /* Half size of the voxels' bounding box */
        Vec3 shape_center;       /* Centre of that box, offset from grid center (local space) */
        Vec3 local_com;          /* Center of mass offset from grid center (local space) */
        float total_mass;        /* Mass from per-material density */
        Vec3 inertia_diag;       /* Diagonal inertia tensor about COM */

        /* Convex hull candidate points (voxel corners, local space): the collision hull
         * and the terrain support of resting bodies */
        Vec3 surface_voxels[VOBJ_MAX_SURFACE_VOXELS];
        int32_t surface_voxel_count;

        bool active;
        bool shape_dirty;       /* Linked in the dirty list for a deferred recalc; only
                                   voxel_object_world_process_recalcs clears it */
        bool split_queued;      /* In the split queue (each object is queued once) */
        int32_t render_delay;   /* Frames to skip rendering (terrain GPU sync) */
        /* Grid cell where the bricks start on each axis, set by voxel_object_recalc_shape:
         * every voxel lies at or after it, in a brick that lies inside the grid */
        uint8_t brick_offset[3];
        /* voxel_revision of the last change inside each brick (vobj_brick_index): a cache
         * of the grid refreshes the bricks whose stamp it has not seen */
        uint32_t brick_revision[VOBJ_BRICK_COUNT];
        bool in_free_list;      /* Slot is on the free list (guards against double free) */
        uint32_t alloc_id;      /* Fresh per allocation: tells a reused slot from its previous occupant */
        int32_t split_source;   /* 1 + index of the object this piece split from (0 = none);
                                   physics copies that object's motion, then clears it */
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

        /* Free-list for O(1) allocation */
        int32_t first_free_slot;
        uint32_t next_alloc_id;

        /* Dirty-list for O(1) recalc lookup */
        int32_t first_dirty;

        /* Deferred split work queue */
        int32_t split_queue[VOBJ_SPLIT_QUEUE_SIZE];
        int32_t split_queue_head;
        int32_t split_queue_tail;

        /* BVH for accelerated object queries */
        BVH *bvh;
    } VoxelObjectWorld;

    typedef struct
    {
        bool hit;
        int32_t object_index;
        Vec3 impact_point;
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

    /* Brick of obj holding its voxel (x, y, z): its bit in brick masks */
    static inline int32_t vobj_brick_index(const VoxelObject *obj, int32_t x, int32_t y, int32_t z)
    {
        return (x - obj->brick_offset[0]) / VOBJ_BRICK_SIZE +
               ((y - obj->brick_offset[1]) / VOBJ_BRICK_SIZE) * VOBJ_BRICKS_PER_AXIS +
               ((z - obj->brick_offset[2]) / VOBJ_BRICK_SIZE) * VOBJ_BRICKS_PER_AXIS * VOBJ_BRICKS_PER_AXIS;
    }

    /* The pose to draw obj with, alpha of a tick past its previous pose (scene_interp_alpha):
     * objects move in fixed steps, drawing glides between them. Physics and queries use the
     * current pose. */
    static inline void voxel_object_draw_pose(const VoxelObject *obj, float alpha, Vec3 *position, Quat *orientation)
    {
        *position = vec3_lerp(obj->prev_position, obj->position, alpha);
        *orientation = quat_nlerp(obj->prev_orientation, obj->orientation, alpha);
    }

    /* World centre of the voxels' bounding box (shape_half_extents are about it) */
    static inline Vec3 voxel_object_box_center(const VoxelObject *obj)
    {
        return vec3_add(obj->position, quat_rotate_vec3(obj->orientation, obj->shape_center));
    }

    VoxelObjectWorld *voxel_object_world_create(Bounds3D bounds, float voxel_size);
    void voxel_object_world_destroy(VoxelObjectWorld *world);

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

    /* Process-wide unique, never-zero revision. Every voxel change and every new
     * object takes a fresh one, so caches keyed on (slot, revision) cannot confuse a
     * reused slot (or a new world at the same address) with its previous occupant. */
    uint32_t voxel_object_next_revision(void);

    /* Records a change of the object's voxels confined to `bricks` (bit vobj_brick_index):
     * takes a fresh voxel_revision and stamps those bricks with it */
    void voxel_object_mark_changed(VoxelObject *obj, uint64_t bricks);

    void voxel_object_recalc_shape(VoxelObject *obj);
    void voxel_object_world_mark_dirty(VoxelObjectWorld *world, int32_t obj_index);
    void voxel_object_world_free_slot(VoxelObjectWorld *world, int32_t slot);

    /* Returns a zeroed, unlinked slot with a fresh alloc_id and voxel_revision (every brick
     * stamped with it), or -1 when the world is full */
    int32_t voxel_object_world_alloc_slot(VoxelObjectWorld *world);

    /* Per-frame deferred processing. A world is used by one thread at a time (these
     * allocate slots); the split scratch lives on the stack, so different worlds may
     * be processed concurrently. */
    void voxel_object_world_process_splits(VoxelObjectWorld *world);
    void voxel_object_world_process_recalcs(VoxelObjectWorld *world);
    void voxel_object_world_tick_render_delays(VoxelObjectWorld *world);
    /* At the start of a simulation tick: every object's pose becomes its previous one */
    void voxel_object_world_begin_tick(VoxelObjectWorld *world);
    void voxel_object_world_queue_split(VoxelObjectWorld *world, int32_t obj_index);

    /* Rebuild (object set changed) or refit (objects moved) the object BVH used by
     * voxel_object_world_raycast and voxel_object_world_test_point. Call once per tick
     * after physics has moved the objects; without it those queries scan linearly. */
    void voxel_object_world_update_bvh(VoxelObjectWorld *world);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_OBJECT_H */
