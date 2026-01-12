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
#define CONNECTIVITY_WORK_STACK_SIZE 65536
#define CONNECTIVITY_MAX_VOXELS_PER_ISLAND 8192

typedef enum
{
    ANCHOR_NONE = 0,
    ANCHOR_FLOOR,
    ANCHOR_MATERIAL,
    ANCHOR_VOLUME_EDGE,
} AnchorType;

typedef struct
{
    Vec3 min_corner;
    Vec3 max_corner;

    int32_t voxel_min_x, voxel_min_y, voxel_min_z;
    int32_t voxel_max_x, voxel_max_y, voxel_max_z;

    int32_t voxel_count;
    Vec3 center_of_mass;  /* Used by detach for spawn position */
    float total_mass;     /* Voxel count as mass; physics may recompute with density */

    AnchorType anchor;
    int32_t island_id;
    bool is_floating;
    uint8_t _pad[3];
} IslandInfo;

typedef struct
{
    IslandInfo islands[CONNECTIVITY_MAX_ISLANDS];
    int32_t island_count;
    int32_t floating_count;
    int32_t anchored_count;
    int32_t total_voxels_checked;
} ConnectivityResult;

typedef struct
{
    int32_t stack[CONNECTIVITY_WORK_STACK_SIZE];
    int32_t stack_top;

    /* Generation-based visited tracking (avoids memset on each call) */
    uint8_t *visited_gen;    /* Per-voxel generation stamp */
    int32_t visited_size;    /* Number of voxels (not bytes) */
    uint8_t generation;      /* Current generation (0 = needs full clear) */

    uint8_t *island_ids;
    int32_t island_ids_size;
} ConnectivityWorkBuffer;

bool connectivity_work_init(ConnectivityWorkBuffer *work, const VoxelVolume *vol);
void connectivity_work_destroy(ConnectivityWorkBuffer *work);
void connectivity_work_clear(ConnectivityWorkBuffer *work);

void connectivity_analyze_region(const VoxelVolume *vol,
                                  Vec3 region_min, Vec3 region_max,
                                  float anchor_y, uint8_t anchor_material,
                                  ConnectivityWorkBuffer *work,
                                  ConnectivityResult *result);

void connectivity_analyze_volume(const VoxelVolume *vol,
                                  float anchor_y, uint8_t anchor_material,
                                  ConnectivityWorkBuffer *work,
                                  ConnectivityResult *result);

void connectivity_analyze_dirty(const VoxelVolume *vol,
                                 float anchor_y, uint8_t anchor_material,
                                 ConnectivityWorkBuffer *work,
                                 ConnectivityResult *result);

int32_t connectivity_extract_island_with_ids(const VoxelVolume *vol,
                                              const IslandInfo *island,
                                              const ConnectivityWorkBuffer *work,
                                              uint8_t *out_voxels,
                                              int32_t out_size_x, int32_t out_size_y, int32_t out_size_z,
                                              Vec3 *out_origin);

void connectivity_remove_island(VoxelVolume *vol, const IslandInfo *island,
                                 const ConnectivityWorkBuffer *work);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_CONNECTIVITY_H */
