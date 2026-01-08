#ifndef PATCH_PHYSICS_VOLUME_CONTACT_H
#define PATCH_PHYSICS_VOLUME_CONTACT_H

#include "engine/core/types.h"
#include "engine/voxel/volume.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Volume Contact Sampling
 *
 * Utilities for detecting and resolving collisions between geometric primitives
 * and voxel volumes. All queries operate on VoxelVolume + ChunkOccupancy.
 */

#define CONTACT_MAX_VOXELS 64

/* Contact info for a single voxel intersection */
typedef struct
{
    Vec3 voxel_center;      /* World-space center of the voxel */
    Vec3 penetration;       /* Penetration vector (direction * depth) */
    float depth;            /* Penetration depth */
    uint8_t material;       /* Material ID of the voxel */
    uint8_t _pad[3];
} VoxelContact;

/* Result of a contact query */
typedef struct
{
    VoxelContact contacts[CONTACT_MAX_VOXELS];
    int32_t count;
    Vec3 average_normal;    /* Average push-out direction */
    float max_depth;        /* Maximum penetration depth */
    bool any_contact;
    uint8_t _pad[3];
} VoxelContactResult;

/*
 * Point vs Volume
 * Returns true if point is inside any solid voxel.
 */
static inline bool volume_contact_point(const VoxelVolume *vol, Vec3 point)
{
    return volume_is_solid_at(vol, point);
}

/*
 * Sphere vs Volume
 * Collects all voxels intersecting a sphere, up to CONTACT_MAX_VOXELS.
 * Returns number of contacts found.
 */
int32_t volume_contact_sphere(const VoxelVolume *vol, Vec3 center, float radius,
                              VoxelContactResult *result);

/*
 * AABB vs Volume
 * Collects all voxels intersecting an axis-aligned bounding box.
 * Returns number of contacts found.
 */
int32_t volume_contact_aabb(const VoxelVolume *vol, Vec3 min_corner, Vec3 max_corner,
                            VoxelContactResult *result);

/*
 * Capsule vs Volume
 * Collects all voxels intersecting a capsule (two spheres connected by cylinder).
 * Returns number of contacts found.
 */
int32_t volume_contact_capsule(const VoxelVolume *vol, Vec3 p0, Vec3 p1, float radius,
                               VoxelContactResult *result);

/*
 * Segment vs Volume
 * Returns first voxel hit along a line segment, or false if no hit.
 * More efficient than raycast for bounded segments.
 */
bool volume_contact_segment(const VoxelVolume *vol, Vec3 start, Vec3 end,
                            Vec3 *out_hit_pos, Vec3 *out_hit_normal, uint8_t *out_material);

/*
 * Resolve penetration
 * Given a contact result, compute the minimum translation vector to resolve overlap.
 * Returns the push-out vector to apply to the object's position.
 */
Vec3 volume_contact_resolve(const VoxelContactResult *result);

/*
 * Sweep sphere
 * Move a sphere along a direction, stopping at first voxel contact.
 * Returns the fraction of movement completed (0-1).
 */
float volume_sweep_sphere(const VoxelVolume *vol, Vec3 start, Vec3 direction, float distance,
                          float radius, Vec3 *out_hit_pos, Vec3 *out_hit_normal);

/*
 * Sweep AABB
 * Move an AABB along a direction, stopping at first voxel contact.
 * Returns the fraction of movement completed (0-1).
 */
float volume_sweep_aabb(const VoxelVolume *vol, Vec3 start, Vec3 half_extents,
                        Vec3 direction, float distance,
                        Vec3 *out_hit_pos, Vec3 *out_hit_normal);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_PHYSICS_VOLUME_CONTACT_H */
