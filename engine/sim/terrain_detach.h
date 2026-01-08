#ifndef PATCH_SIM_TERRAIN_DETACH_H
#define PATCH_SIM_TERRAIN_DETACH_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "engine/sim/voxel_object.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Terrain Detach System
 *
 * Converts floating islands from VoxelVolume into VoxelObjects.
 * Triggered after voxel edits (e.g., destruction).
 *
 * Workflow:
 * 1. Scene calls volume_edit_begin/set/end to modify terrain
 * 2. Scene calls terrain_detach_process() with dirty region
 * 3. System analyzes connectivity, extracts floating islands
 * 4. Islands become VoxelObjects with physics
 */

/* Configuration for terrain detach behavior (scene policy) */
typedef struct
{
    bool enabled;                   /* Master toggle */
    int32_t max_islands_per_tick;   /* Cap islands processed per call */
    int32_t max_voxels_per_island;  /* Islands larger than this stay as terrain */
    int32_t min_voxels_per_island;  /* Islands smaller than this are deleted (particles) */
    int32_t max_bodies_alive;       /* Cap total active objects */
    float anchor_y_offset;          /* Y offset from bounds.min_y for anchor detection */
    Vec3 initial_impulse_scale;     /* Scale for initial velocity from impact */
} TerrainDetachConfig;

/* Result of terrain detach processing */
typedef struct
{
    int32_t islands_processed;
    int32_t bodies_spawned;
    int32_t voxels_removed;         /* Deleted due to min_voxels threshold */
    int32_t islands_skipped;        /* Skipped due to max_voxels or capacity */
} TerrainDetachResult;

/* Default config with reasonable values */
static inline TerrainDetachConfig terrain_detach_config_default(void)
{
    TerrainDetachConfig cfg;
    cfg.enabled = true;
    cfg.max_islands_per_tick = 8;
    cfg.max_voxels_per_island = VOBJ_TOTAL_VOXELS; /* 4096 */
    cfg.min_voxels_per_island = 4;
    cfg.max_bodies_alive = VOBJ_MAX_OBJECTS - 8;   /* Reserve some slots */
    cfg.anchor_y_offset = 0.1f;
    cfg.initial_impulse_scale = vec3_create(2.0f, 4.0f, 2.0f);
    return cfg;
}

/*
 * Process terrain detachment after voxel edits.
 *
 * vol: The volume that was edited (with dirty chunks)
 * obj_world: Target for spawned voxel bodies
 * config: Scene policy for detach behavior
 * work: Connectivity work buffer (caller-provided, reusable)
 * impact_point: Center of destruction (for initial impulse direction)
 * rng: RNG state for randomness
 * result: Output stats (optional, can be NULL)
 *
 * Call after volume_edit_end() when voxels have been removed.
 */
void terrain_detach_process(VoxelVolume *vol,
                            VoxelObjectWorld *obj_world,
                            const TerrainDetachConfig *config,
                            ConnectivityWorkBuffer *work,
                            Vec3 impact_point,
                            RngState *rng,
                            TerrainDetachResult *result);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_SIM_TERRAIN_DETACH_H */
