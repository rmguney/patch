#ifndef PATCH_SIM_DETACH_H
#define PATCH_SIM_DETACH_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/voxel/voxel_object.h"
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
    int32_t max_voxels_per_island;
    int32_t min_voxels_per_island;
    int32_t max_bodies_alive;
    float anchor_y_offset;
} DetachConfig;

/* Result of terrain detach processing */
typedef struct
{
    int32_t islands_processed;
    int32_t bodies_spawned;
    int32_t voxels_removed;
    int32_t islands_skipped;
} DetachResult;

/* Default config */
static inline DetachConfig detach_config_default(void)
{
    DetachConfig cfg;
    cfg.enabled = true;
    cfg.max_islands_per_tick = 8;
    cfg.max_voxels_per_island = VOBJ_TOTAL_VOXELS;
    cfg.min_voxels_per_island = 4;
    cfg.max_bodies_alive = VOBJ_MAX_OBJECTS - 8;
    cfg.anchor_y_offset = 0.1f;
    return cfg;
}

/*
 * Destroy voxels at a point on an object.
 * Automatically splits disconnected islands into separate objects.
 *
 * Returns number of voxels destroyed.
 * Optionally outputs destroyed voxel positions and materials.
 */
int32_t detach_object_at_point(VoxelObjectWorld *world, int32_t obj_index,
                               Vec3 impact_point, float destroy_radius,
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
                            DetachResult *result);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_SIM_DETACH_H */
