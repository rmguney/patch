#ifndef PATCH_VOXEL_CONNECTIVITY_H
#define PATCH_VOXEL_CONNECTIVITY_H

#include "engine/voxel/volume.h"
#include "engine/core/types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Voxel Connectivity / Island Detection
 *
 * Flood-fill based connectivity detection for voxel volumes.
 * Used to identify disconnected islands after voxel destruction.
 *
 * - Anchor detection uses explicit criteria (floor contact, anchor materials)
 */

#define CONNECTIVITY_MAX_ISLANDS 64
#define CONNECTIVITY_WORK_STACK_SIZE 16384
#define CONNECTIVITY_MAX_VOXELS_PER_ISLAND 8192

/*
 * Stack overflow policy: If flood fill exceeds CONNECTIVITY_WORK_STACK_SIZE,
 * the island is marked as anchored (ANCHOR_FLOOR) to prevent incorrect
 * fragmentation. This is a fail-safe: better to keep large structures
 * attached than incorrectly split them. Typical use cases with voxel_size
 * ~0.1 and reasonable destruction radii stay well under this limit.
 */

/* Anchor classification */
typedef enum
{
    ANCHOR_NONE = 0,
    ANCHOR_FLOOR,           /* Touching floor (y == min_y) */
    ANCHOR_MATERIAL,        /* Contains anchor material (e.g., foundation blocks) */
    ANCHOR_VOLUME_EDGE,     /* Touching volume boundary */
} AnchorType;

/* Island descriptor */
typedef struct
{
    /* Bounding box in world space */
    Vec3 min_corner;
    Vec3 max_corner;

    /* Bounding box in voxel coordinates */
    int32_t voxel_min_x, voxel_min_y, voxel_min_z;
    int32_t voxel_max_x, voxel_max_y, voxel_max_z;

    int32_t voxel_count;
    Vec3 center_of_mass;
    float total_mass;

    AnchorType anchor;
    int32_t island_id;
    bool is_floating;       /* True if not anchored (candidate for fragment) */
    uint8_t _pad[3];
} IslandInfo;

/* Result of connectivity analysis */
typedef struct
{
    IslandInfo islands[CONNECTIVITY_MAX_ISLANDS];
    int32_t island_count;
    int32_t floating_count;     /* Number of unanchored islands */
    int32_t anchored_count;     /* Number of anchored islands */
    int32_t total_voxels_checked;
} ConnectivityResult;

/* Work buffer for flood fill (caller provides to avoid allocation) */
typedef struct
{
    /* Stack for flood fill */
    int32_t stack[CONNECTIVITY_WORK_STACK_SIZE];
    int32_t stack_top;

    /* Visited flags (one bit per voxel, packed) */
    uint8_t *visited;
    int32_t visited_size;

    /* Island assignment per voxel (0 = unassigned) */
    uint8_t *island_ids;
    int32_t island_ids_size;
} ConnectivityWorkBuffer;

/*
 * Initialize work buffer for a volume.
 * Allocates visited/island_ids arrays sized for the volume.
 * Returns false if allocation fails.
 */
bool connectivity_work_init(ConnectivityWorkBuffer *work, const VoxelVolume *vol);

/*
 * Free work buffer resources.
 */
void connectivity_work_destroy(ConnectivityWorkBuffer *work);

/*
 * Clear work buffer for reuse (same volume).
 */
void connectivity_work_clear(ConnectivityWorkBuffer *work);

/*
 * Analyze connectivity in a region of the volume.
 * Only checks voxels within the specified bounds (world space).
 * Populates result with island information.
 *
 * anchor_y: Y coordinate below which voxels are considered anchored to floor.
 * anchor_material: Material ID that counts as an anchor (0 to disable).
 */
void connectivity_analyze_region(const VoxelVolume *vol,
                                  Vec3 region_min, Vec3 region_max,
                                  float anchor_y, uint8_t anchor_material,
                                  ConnectivityWorkBuffer *work,
                                  ConnectivityResult *result);

/*
 * Analyze connectivity for the entire volume.
 */
void connectivity_analyze_volume(const VoxelVolume *vol,
                                  float anchor_y, uint8_t anchor_material,
                                  ConnectivityWorkBuffer *work,
                                  ConnectivityResult *result);

/*
 * Analyze connectivity in chunks affected by recent edits.
 * More efficient than full volume analysis after small edits.
 * Uses the volume's dirty chunk tracking.
 */
void connectivity_analyze_dirty(const VoxelVolume *vol,
                                 float anchor_y, uint8_t anchor_material,
                                 ConnectivityWorkBuffer *work,
                                 ConnectivityResult *result);

/*
 * Extract voxel data for a floating island (DEPRECATED - use _with_ids).
 * Only sets up origin and returns voxel count. Does not copy voxels.
 */
int32_t connectivity_extract_island(const VoxelVolume *vol,
                                     const IslandInfo *island,
                                     uint8_t *out_voxels,
                                     int32_t out_size_x, int32_t out_size_y, int32_t out_size_z,
                                     Vec3 *out_origin);

/*
 * Extract voxel data for a floating island using island ID filtering.
 * Copies only voxels belonging to this specific island (verified via work buffer).
 * Returns number of solid voxels copied.
 *
 * out_voxels: Must be sized at least (island.voxel_max - island.voxel_min + 1) per axis.
 * out_origin: World-space origin of the extracted voxel region.
 */
int32_t connectivity_extract_island_with_ids(const VoxelVolume *vol,
                                              const IslandInfo *island,
                                              const ConnectivityWorkBuffer *work,
                                              uint8_t *out_voxels,
                                              int32_t out_size_x, int32_t out_size_y, int32_t out_size_z,
                                              Vec3 *out_origin);

/*
 * Remove a floating island from the volume.
 * Clears all voxels belonging to the island (sets to MATERIAL_EMPTY).
 * Call after extracting island data for fragment creation.
 */
void connectivity_remove_island(VoxelVolume *vol, const IslandInfo *island,
                                 const ConnectivityWorkBuffer *work);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_CONNECTIVITY_H */
