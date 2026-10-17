#ifndef PATCH_VOXEL_CHUNK_H
#define PATCH_VOXEL_CHUNK_H

#include "engine/core/types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CHUNK_SIZE_BITS 5
#define CHUNK_SIZE (1 << CHUNK_SIZE_BITS)
#define CHUNK_SIZE_MASK (CHUNK_SIZE - 1)
#define CHUNK_VOXEL_COUNT (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)

#define MATERIAL_EMPTY VOXEL_MATERIAL_EMPTY /* 0 = air/empty */

    /*
     * VoxelCell: minimal payload per voxel.
     * Occupancy is implicit: material != MATERIAL_EMPTY means occupied.
     */
    typedef struct
    {
        uint8_t material;
    } VoxelCell;

STATIC_ASSERT(sizeof(VoxelCell) == 1, "VoxelCell must be 1 byte");

    /* Chunk lifecycle state (GPU upload tracking). A zeroed chunk is empty and ACTIVE,
     * so a calloc'd volume needs no initialisation pass. */
    typedef enum
    {
        CHUNK_STATE_ACTIVE, /* GPU copy is current */
        CHUNK_STATE_DIRTY   /* Modified, needs GPU upload */
    } ChunkState;

/*
 * Hierarchical occupancy for traversal acceleration.
 * Each level is a bitmask where 1 = subtree contains solid voxels.
 * Level 0: 4x4x4 = 64 bits (8 bytes) covering 8x8x8 voxel regions
 * Level 1: 2x2x2 = 8 bits covering 16x16x16 regions
 * Level 2: 1 bit for entire chunk
 */
#define CHUNK_MIP0_SIZE 4
#define CHUNK_MIP1_SIZE 2

    typedef struct
    {
        uint64_t level0;      /* 64 bits: 4x4x4 regions of 8x8x8 voxels */
        uint8_t level1;       /* 8 bits: 2x2x2 regions of 16x16x16 voxels */
        uint8_t has_any;      /* 1 if any voxel is solid */
        uint16_t solid_count; /* Number of solid voxels (for quick empty check) */
    } ChunkOccupancy;

    /*
     * Chunk: a fixed-size cube of voxels with metadata.
     */
    typedef struct
    {
        VoxelCell voxels[CHUNK_VOXEL_COUNT];
        ChunkOccupancy occupancy;
        ChunkState state;
        /* Level-0 regions an open edit batch emptied voxels in. Their occupancy bits may
         * still be set although the region is empty (never the reverse), until
         * volume_edit_end rescans them once. */
        uint64_t stale_regions;
    } Chunk;

    /* Linear index from local voxel coordinates within chunk */
    static inline int32_t chunk_voxel_index(int32_t x, int32_t y, int32_t z)
    {
        return x + (y << CHUNK_SIZE_BITS) + (z << (CHUNK_SIZE_BITS * 2));
    }

    /* Check if local coordinates are within chunk bounds */
    static inline bool chunk_in_bounds(int32_t x, int32_t y, int32_t z)
    {
        return (x >= 0 && x < CHUNK_SIZE &&
                y >= 0 && y < CHUNK_SIZE &&
                z >= 0 && z < CHUNK_SIZE);
    }

    /* Get voxel material at local coordinates */
    static inline uint8_t chunk_get(const Chunk *chunk, int32_t x, int32_t y, int32_t z)
    {
        if (!chunk_in_bounds(x, y, z))
            return MATERIAL_EMPTY;
        return chunk->voxels[chunk_voxel_index(x, y, z)].material;
    }

    /* Level-0 occupancy bit of the 8x8x8 region holding local voxel (x, y, z) */
    static inline uint64_t chunk_region_bit(int32_t x, int32_t y, int32_t z)
    {
        return 1ULL << ((x >> 3) + (y >> 3) * CHUNK_MIP0_SIZE + (z >> 3) * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE);
    }

    /* Level-1 occupancy bit of the 16x16x16 region holding local voxel (x, y, z) */
    static inline uint8_t chunk_level1_bit(int32_t x, int32_t y, int32_t z)
    {
        return (uint8_t)(1u << ((x >> 4) + (y >> 4) * CHUNK_MIP1_SIZE + (z >> 4) * CHUNK_MIP1_SIZE * CHUNK_MIP1_SIZE));
    }

    /* Rescan the level-0 regions in `regions` (a level-0 bit mask), then derive level1 */
    void chunk_refresh_regions(Chunk *chunk, uint64_t regions);

    /* Set voxel material at local coordinates, keeping the occupancy exact */
    static inline void chunk_set(Chunk *chunk, int32_t x, int32_t y, int32_t z, uint8_t material)
    {
        if (!chunk_in_bounds(x, y, z))
            return;
        int32_t idx = chunk_voxel_index(x, y, z);
        uint8_t old_mat = chunk->voxels[idx].material;
        if (old_mat == material)
            return;

        chunk->voxels[idx].material = material;

        /* A new solid voxel just sets its region bits; an emptied one may have been
         * its region's last, so that region needs a rescan */
        if (old_mat == MATERIAL_EMPTY)
        {
            chunk->occupancy.solid_count++;
            chunk->occupancy.level0 |= chunk_region_bit(x, y, z);
            chunk->occupancy.level1 |= chunk_level1_bit(x, y, z);
        }
        else if (material == MATERIAL_EMPTY)
        {
            chunk->occupancy.solid_count--;
            chunk_refresh_regions(chunk, chunk_region_bit(x, y, z));
        }
        chunk->occupancy.has_any = chunk->occupancy.solid_count > 0 ? 1 : 0;

        if (chunk->state == CHUNK_STATE_ACTIVE)
            chunk->state = CHUNK_STATE_DIRTY;
    }

    /* Rebuild hierarchical occupancy and the solid count from voxel data */
    void chunk_rebuild_occupancy(Chunk *chunk);

#ifdef __cplusplus
}
#endif

#endif
