#ifndef PATCH_SIM_DETACH_H
#define PATCH_SIM_DETACH_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include "engine/core/rng.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Detach System
 *
 * Handles destruction and splitting mechanics:
 * 1. Object destruction - remove voxels, split disconnected parts
 * 2. Terrain detachment - convert floating terrain islands to objects
 */

/* Configuration for terrain detach behavior */
typedef struct
{
    bool enabled;
    int32_t max_islands_per_tick;
    int32_t min_voxels_per_island;
    int32_t max_bodies_alive;
    int32_t particle_voxel_threshold;
    float anchor_y_offset;
} DetachConfig;

#define DETACH_MAX_SPAWNED 16

/* Result of terrain detach processing */
typedef struct
{
    int32_t islands_processed;
    int32_t bodies_spawned;
    int32_t voxels_removed;
    int32_t islands_skipped; /* Removed as debris: max_bodies_alive was reached */
    int32_t particles_spawned;
    int32_t spawned_indices[DETACH_MAX_SPAWNED];
    bool analysis_requeued; /* Some floating islands were left for the next call */
} DetachResult;

/* Default config */
static inline DetachConfig detach_config_default(void)
{
    DetachConfig cfg;
    cfg.enabled = true;
    cfg.max_islands_per_tick = 64;
    cfg.min_voxels_per_island = 4;
    cfg.max_bodies_alive = VOBJ_MAX_OBJECTS - 8;
    cfg.particle_voxel_threshold = 64;
    cfg.anchor_y_offset = 0.1f;
    return cfg;
}

/*
 * Destroy the object's voxels within destroy_radius of impact_point. Shape recalc
 * and splitting off disconnected pieces are queued for
 * voxel_object_world_process_recalcs / voxel_object_world_process_splits.
 *
 * Returns number of voxels destroyed.
 * Optionally outputs destroyed voxel positions and materials.
 */
int32_t detach_object_at_point(VoxelObjectWorld *world, int32_t obj_index,
                               Vec3 impact_point, float destroy_radius,
                               Vec3 *out_positions, uint8_t *out_materials,
                               int32_t max_output);

/* Largest terrain destruction radius: the sphere must fit one edit batch, or
 * volume_edit_set would drop edits past the budget while they are still reported */
#define DETACH_TERRAIN_MAX_RADIUS_VOXELS 7

/*
 * Destroy the breakable terrain voxels at the whole-voxel offsets from center within
 * radius_voxels (clamped to DETACH_TERRAIN_MAX_RADIUS_VOXELS). Integer offsets, not
 * float steps: accumulated rounding used to drop the sphere's axis tips. Done as one
 * edit batch, which queues the cut for detach_terrain_process.
 *
 * Returns number of voxels destroyed.
 * Optionally outputs destroyed voxel positions and materials.
 */
int32_t detach_terrain_at_point(VoxelVolume *vol, Vec3 center, int32_t radius_voxels,
                                Vec3 *out_positions, uint8_t *out_materials,
                                int32_t max_output);

/*
 * Process terrain detachment after voxel edits.
 * Finds floating islands and converts them to voxel objects.
 *
 * Call after volume_edit_end() when voxels have been removed.
 */
void detach_terrain_process(VoxelVolume *vol,
                            VoxelObjectWorld *obj_world,
                            const DetachConfig *config,
                            ConnectivityWorkBuffer *work,
                            DetachResult *result,
                            ParticleSystem *particles,
                            RngState *rng);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_SIM_DETACH_H */
