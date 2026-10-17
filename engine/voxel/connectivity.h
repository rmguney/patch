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

#define CONNECTIVITY_MAX_ISLANDS 64
#define CONNECTIVITY_MIN_STACK_SIZE 65536
#define CONNECTIVITY_MAX_STACK_SIZE 1048576
#define CONNECTIVITY_MAX_ISLAND_IDS 65536 /* island ids are uint16_t; 0 = none */

/* A floating island: a connected component of non-liquid voxels with no voxel at the
 * anchor floor */
typedef struct
{
    int32_t voxel_min_x, voxel_min_y, voxel_min_z; /* Volume voxel bounds, inclusive */
    int32_t voxel_max_x, voxel_max_y, voxel_max_z;

    int32_t voxel_count;
    Vec3 center_of_mass; /* World space, uniform voxel mass */
    int32_t island_id;   /* Value its voxels carry in ConnectivityWorkBuffer.island_ids */
} IslandInfo;

typedef struct
{
    IslandInfo islands[CONNECTIVITY_MAX_ISLANDS];
    int32_t island_count;
    bool truncated; /* Seeds left unanalysed because islands[] or the island ids ran out */
} ConnectivityResult;

typedef struct
{
    int32_t *stack;          /* Flood-fill stack of packed voxel positions */
    int32_t stack_capacity;
    int32_t stack_top;

    /* Generation-based visited tracking (avoids memset on each call) */
    uint16_t *visited_gen;   /* Per-voxel generation stamp, by volume_voxel_linear_index */
    size_t visited_size;     /* Number of voxels (not bytes) */
    uint16_t generation;     /* Current generation; 0 is never current */

    uint16_t *island_ids;    /* Per-voxel island id of the last analysis (0 = none) */

    /* Chunks whose island_ids slice holds non-zero ids, so a clear only touches
     * those 64 KB slices instead of the whole (volume-sized) buffer. */
    int32_t *id_dirty_chunks;
    uint8_t *id_chunk_dirty;
    int32_t id_dirty_count;

    uint8_t *anchored_ids; /* Anchor flag per island id (CONNECTIVITY_MAX_ISLAND_IDS entries) */

    /* Per material: voxels of it belong to islands. Air and liquids do not: water neither
     * holds terrain up nor leaves with it. */
    bool joins[VOXEL_MATERIAL_MAX];
} ConnectivityWorkBuffer;

bool connectivity_work_init(ConnectivityWorkBuffer *work, const VoxelVolume *vol);
void connectivity_work_destroy(ConnectivityWorkBuffer *work);

/* Find the floating islands created by the edits last handed to analysis
 * (volume_consume_pending_analysis): flood fill from vol->last_cut_boundary, or from
 * every solid voxel of the edited chunks when there is no usable cut boundary. A
 * component is anchored if it reaches the voxel layer at anchor_y. */
void connectivity_analyze_dirty(const VoxelVolume *vol, float anchor_y,
                                ConnectivityWorkBuffer *work, ConnectivityResult *result);

/* Copy an island's voxels (by id) into out_voxels, an out_size_x * out_size_y *
 * out_size_z grid whose origin is the island's minimum voxel corner. Returns the
 * number of voxels copied, 0 if the island does not fit. */
int32_t connectivity_extract_island_with_ids(const VoxelVolume *vol,
                                             const IslandInfo *island,
                                             const ConnectivityWorkBuffer *work,
                                             uint8_t *out_voxels,
                                             int32_t out_size_x, int32_t out_size_y, int32_t out_size_z,
                                             Vec3 *out_origin);

/* Clear an island's voxels from the volume (as one edit batch, logged in the volume's
 * removal log so bodies resting on the island wake up) */
void connectivity_remove_island(VoxelVolume *vol, const IslandInfo *island,
                                const ConnectivityWorkBuffer *work);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_CONNECTIVITY_H */
