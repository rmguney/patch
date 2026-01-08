#ifndef PATCH_SIM_VOXEL_OBJECT_H
#define PATCH_SIM_VOXEL_OBJECT_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/core/spatial_hash.h"
#include "engine/voxel/volume.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Voxel Object System (Entity Layer)
     *
     * Voxel objects separate from terrain VoxelVolume.
     * Each object owns a small voxel grid with explicit transform/velocities.
     *
     * This file handles entity management: creation, destruction, raycasting.
     * Physics simulation is in engine/physics/voxel_body.h.
     */

#define VOBJ_GRID_SIZE 16
#define VOBJ_TOTAL_VOXELS (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE)
#define VOBJ_MAX_OBJECTS 4096

    /* Single voxel in object grid */
    typedef struct
    {
        uint8_t material; /* 0 = empty */
    } VObjVoxel;

    /* Voxel rigid body */
    typedef struct VoxelObject
    {
        /* Transform */
        Vec3 position;
        Vec3 velocity;
        Quat orientation;      /* Quaternion rotation (replaces Euler angles) */
        Vec3 rotation;         /* Euler angles - DEPRECATED, kept for renderer transition */
        Vec3 angular_velocity; /* Radians per second */

        /* Inertia tensor (3x3 stored as 9 floats, row-major) */
        float inv_inertia_local[9];  /* Local-space inverse inertia (computed once) */
        float inv_inertia_world[9];  /* World-space inverse inertia (updated each frame) */

        /* Shape (derived from voxels) */
        Vec3 center_of_mass_offset; /* Offset from position to center of mass */
        Vec3 shape_half_extents;    /* AABB half extents */
        float radius;               /* Bounding sphere for broadphase */
        float mass;
        float inv_mass;             /* 1.0f / mass (computed once) */

        /* Support polygon (for topple torque) */
        Vec3 support_min;
        Vec3 support_max;

        /* Cached rotated bounds (avoid per-frame voxel iteration) */
        Vec3 cached_rotation;     /* Rotation when bounds were computed */
        float cached_lowest_y;    /* World-space lowest point */
        float cached_highest_y;   /* World-space highest point */
        float cached_bounds_x[2]; /* min_x, max_x in world space */
        float cached_bounds_z[2]; /* min_z, max_z in world space */
        bool bounds_dirty;        /* Force recompute */

        /* Voxel grid */
        VObjVoxel voxels[VOBJ_TOTAL_VOXELS];
        float voxel_size; /* World units per voxel */
        int32_t voxel_count;

        /* State */
        bool active;
        bool sleeping;  /* True when object has come to rest */
        bool on_ground; /* True when touching floor */

        /* Lifetime management */
        float settle_timer; /* Time object has been nearly stationary */
        float lifetime;     /* Total time alive */
    } VoxelObject;

    /* World containing voxel objects */
    typedef struct
    {
        VoxelObject objects[VOBJ_MAX_OBJECTS];
        int32_t object_count;

        Bounds3D bounds;
        float voxel_size; /* Default voxel size for objects in this world */

        /* Physics parameters */
        Vec3 gravity;
        float damping;
        float angular_damping;
        float restitution;
        float floor_friction;

        bool enable_object_collision;
        SpatialHashGrid collision_grid;

        /* Optional terrain for collision (set via voxel_object_world_set_terrain) */
        VoxelVolume *terrain;
    } VoxelObjectWorld;

    /* Raycast hit result */
    typedef struct
    {
        bool hit;
        int32_t object_index;
        Vec3 impact_point;
        Vec3 impact_normal;
        Vec3 impact_normal_local;
        int32_t voxel_x, voxel_y, voxel_z;
    } VoxelObjectHit;

    /*
     * Index helpers
     */
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

    /*
     * World lifecycle
     */
    VoxelObjectWorld *voxel_object_world_create(Bounds3D bounds, float voxel_size);
    void voxel_object_world_destroy(VoxelObjectWorld *world);

    /*
     * Set optional terrain for collision detection.
     * Pass NULL to disable terrain collision.
     */
    void voxel_object_world_set_terrain(VoxelObjectWorld *world, VoxelVolume *terrain);

    /*
     * Object creation
     */
    int32_t voxel_object_world_add_sphere(VoxelObjectWorld *world, Vec3 position,
                                          float radius, uint8_t material, RngState *rng);
    int32_t voxel_object_world_add_box(VoxelObjectWorld *world, Vec3 position,
                                       Vec3 half_extents, uint8_t material, RngState *rng);

    /*
     * Raycasting
     */
    VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld *world, Vec3 origin, Vec3 dir);

    /*
     * Destruction
     * Returns number of voxels destroyed.
     * out_positions/out_materials: optional arrays to receive destroyed voxel info (for particles).
     */
    int32_t voxel_object_destroy_at_point(VoxelObjectWorld *world, int32_t obj_index,
                                          Vec3 impact_point, float destroy_radius,
                                          Vec3 *out_positions, uint8_t *out_materials,
                                          int32_t max_output);

    /*
     * Add a VoxelObject from extracted voxels.
     * Used by terrain_detach_process, exposed for manual spawning.
     *
     * voxels: Flat array of materials (0 = empty)
     * size_x/y/z: Dimensions of voxel array
     * origin: World-space position of voxel[0,0,0]
     * voxel_size: Size of each voxel in world units
     * initial_velocity: Starting velocity for the object
     * rng: RNG for angular velocity
     *
     * Returns object index or -1 if pool is full.
     */
    int32_t voxel_object_world_add_from_voxels(VoxelObjectWorld *world,
                                               const uint8_t *voxels,
                                               int32_t size_x, int32_t size_y, int32_t size_z,
                                               Vec3 origin, float voxel_size,
                                               Vec3 initial_velocity,
                                               RngState *rng);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_SIM_VOXEL_OBJECT_H */
