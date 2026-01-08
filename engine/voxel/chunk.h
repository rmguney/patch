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

#define MATERIAL_EMPTY VOXEL_MATERIAL_EMPTY   /* 0 = air/empty */
#define MATERIAL_MAX (VOXEL_MATERIAL_MAX - 1) /* 255 = max valid material ID */

    /*
     * VoxelCell: minimal payload per voxel.
     * Occupancy is implicit: material != MATERIAL_EMPTY means occupied.
     */
    typedef struct
    {
        uint8_t material;
    } VoxelCell;

#ifdef __cplusplus
    static_assert(sizeof(VoxelCell) == 1, "VoxelCell must be 1 byte");
#else
_Static_assert(sizeof(VoxelCell) == 1, "VoxelCell must be 1 byte");
#endif

    /*
     * Chunk state for lifecycle management.
     * Inspired by REF_GUIDE: chunk lifecycle state machine with explicit transitions.
     */
    typedef enum
    {
        CHUNK_STATE_EMPTY,    /* No voxel data, not allocated */
        CHUNK_STATE_LOADING,  /* Being populated (generation or load) */
        CHUNK_STATE_ACTIVE,   /* Live simulation data */
        CHUNK_STATE_DIRTY,    /* Modified, needs GPU upload */
        CHUNK_STATE_UPLOADING /* Being sent to GPU */
    } ChunkState;

/*
 * Hierarchical occupancy for traversal acceleration.
 * Each level is a bitmask where 1 = subtree contains solid voxels.
 * Level 0: 4x4x4 = 64 bits (8 bytes) covering 8x8x8 voxel regions
 * Level 1: 2x2x2 = 8 bits covering 16x16x16 regions
 * Level 2: 1 bit for entire chunk
 */
#define CHUNK_MIP0_SIZE 4
#define CHUNK_MIP0_BITS (CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE)
#define CHUNK_MIP1_SIZE 2
#define CHUNK_MIP1_BITS (CHUNK_MIP1_SIZE * CHUNK_MIP1_SIZE * CHUNK_MIP1_SIZE)

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
        uint32_t dirty_frame;              /* Frame when last modified (for upload scheduling) */
        int32_t coord_x, coord_y, coord_z; /* Chunk coordinates in volume */
    } Chunk;

    /* Linear index from local voxel coordinates within chunk */
    static inline int32_t chunk_voxel_index(int32_t x, int32_t y, int32_t z)
    {
        return x + (y << CHUNK_SIZE_BITS) + (z << (CHUNK_SIZE_BITS * 2));
    }

    /* Extract local coordinates from linear index */
    static inline void chunk_voxel_coords(int32_t index, int32_t *x, int32_t *y, int32_t *z)
    {
        *x = index & CHUNK_SIZE_MASK;
        *y = (index >> CHUNK_SIZE_BITS) & CHUNK_SIZE_MASK;
        *z = (index >> (CHUNK_SIZE_BITS * 2)) & CHUNK_SIZE_MASK;
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

    /* Forward declaration for incremental occupancy update */
    void chunk_update_occupancy_region(Chunk *chunk, int32_t region_x, int32_t region_y, int32_t region_z);

    /* Set voxel material at local coordinates */
    static inline void chunk_set(Chunk *chunk, int32_t x, int32_t y, int32_t z, uint8_t material)
    {
        if (!chunk_in_bounds(x, y, z))
            return;
        int32_t idx = chunk_voxel_index(x, y, z);
        uint8_t old_mat = chunk->voxels[idx].material;

        if (old_mat != material)
        {
            chunk->voxels[idx].material = material;

            /* Update solid count */
            if (old_mat == MATERIAL_EMPTY && material != MATERIAL_EMPTY)
            {
                chunk->occupancy.solid_count++;
            }
            else if (old_mat != MATERIAL_EMPTY && material == MATERIAL_EMPTY)
            {
                chunk->occupancy.solid_count--;
            }

            chunk->occupancy.has_any = (chunk->occupancy.solid_count > 0) ? 1 : 0;

            /* Update hierarchical occupancy for the affected region */
            chunk_update_occupancy_region(chunk, x / 8, y / 8, z / 8);

            if (chunk->state == CHUNK_STATE_ACTIVE)
            {
                chunk->state = CHUNK_STATE_DIRTY;
            }
        }
    }

    /* Check if voxel at coordinates is solid (non-empty) */
    static inline bool chunk_is_solid(const Chunk *chunk, int32_t x, int32_t y, int32_t z)
    {
        return chunk_get(chunk, x, y, z) != MATERIAL_EMPTY;
    }

    /* Initialize chunk to empty state */
    static inline void chunk_init(Chunk *chunk, int32_t cx, int32_t cy, int32_t cz)
    {
        for (int32_t i = 0; i < CHUNK_VOXEL_COUNT; i++)
        {
            chunk->voxels[i].material = MATERIAL_EMPTY;
        }
        chunk->occupancy.level0 = 0;
        chunk->occupancy.level1 = 0;
        chunk->occupancy.has_any = 0;
        chunk->occupancy.solid_count = 0;
        chunk->state = CHUNK_STATE_EMPTY;
        chunk->dirty_frame = 0;
        chunk->coord_x = cx;
        chunk->coord_y = cy;
        chunk->coord_z = cz;
    }

    /* Rebuild hierarchical occupancy from voxel data */
    void chunk_rebuild_occupancy(Chunk *chunk);

    /* Update occupancy for voxels in the given local coordinate range (inclusive) */
    void chunk_update_occupancy_range(Chunk *chunk, int32_t x0, int32_t y0, int32_t z0,
                                      int32_t x1, int32_t y1, int32_t z1);

    /* Fill chunk with a single material */
    void chunk_fill(Chunk *chunk, uint8_t material);

    /* Fill sphere within chunk (local coordinates, returns voxels modified) */
    int32_t chunk_fill_sphere(Chunk *chunk, float cx, float cy, float cz, float radius, uint8_t material);

    /* Fill box within chunk (local coordinates, returns voxels modified) */
    int32_t chunk_fill_box(Chunk *chunk, int32_t x0, int32_t y0, int32_t z0,
                           int32_t x1, int32_t y1, int32_t z1, uint8_t material);

#ifdef __cplusplus
}
#endif

#endif
