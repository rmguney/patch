#ifndef PATCH_PHYSICS_VOXEL_BODY_H
#define PATCH_PHYSICS_VOXEL_BODY_H

#include "engine/sim/voxel_object.h"
#include "engine/voxel/volume.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Voxel Body Physics
 *
 * Physics simulation for VoxelObject rigid bodies.
 * Separated from entity management (voxel_object.h) per architecture rules.
 *
 * Handles:
 * - Gravity and damping integration
 * - Rotated voxel-accurate ground/wall collision
 * - Object-object sphere collision
 * - Topple torque for unstable objects
 * - Terrain collision (optional)
 */

/*
 * Physics update (fixed timestep).
 * Integrates velocities, applies gravity, resolves collisions.
 */
void voxel_body_world_update(VoxelObjectWorld *world, float dt);

/*
 * Physics update with terrain collision.
 * terrain can be NULL for no terrain collision.
 */
void voxel_body_world_update_with_terrain(VoxelObjectWorld *world, VoxelVolume *terrain, float dt);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_PHYSICS_VOXEL_BODY_H */
