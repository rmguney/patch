#include "chunk.h"
#include <string.h>

/* The 8 voxels of one level-0 region row (x fastest) are contiguous bytes: read them as
 * one word */
static inline uint64_t load_row8(const VoxelCell *row)
{
    uint64_t word;
    memcpy(&word, row, sizeof(word));
    return word;
}

static bool region_has_solid(const Chunk *chunk, int32_t region)
{
    int32_t base_x = (region % CHUNK_MIP0_SIZE) * 8;
    int32_t base_y = (region / CHUNK_MIP0_SIZE % CHUNK_MIP0_SIZE) * 8;
    int32_t base_z = region / (CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE) * 8;
    for (int32_t z = 0; z < 8; z++)
    {
        for (int32_t y = 0; y < 8; y++)
        {
            if (load_row8(&chunk->voxels[chunk_voxel_index(base_x, base_y + y, base_z + z)]) != 0)
                return true;
        }
    }
    return false;
}

/* Level-1 bit b covers the 2x2x2 level-0 regions of its octant */
static uint8_t level1_from_level0(uint64_t level0)
{
    uint8_t level1 = 0;
    for (int32_t b = 0; b < 8; b++)
    {
        /* Level-0 bits (0,0,0), (1,0,0), (0,1,0), (1,1,0) and the same one z layer up,
         * moved to the octant */
        int32_t shift = (b & 1) * 2 + ((b >> 1) & 1) * 2 * CHUNK_MIP0_SIZE +
                        (b >> 2) * 2 * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;
        if (level0 & (0x330033ULL << shift))
            level1 |= (uint8_t)(1u << b);
    }
    return level1;
}

void chunk_refresh_regions(Chunk *chunk, uint64_t regions)
{
    uint64_t level0 = chunk->occupancy.level0;
    for (int32_t region = 0; regions != 0; region++, regions >>= 1)
    {
        if (!(regions & 1))
            continue;
        uint64_t bit = 1ULL << region;
        if (region_has_solid(chunk, region))
            level0 |= bit;
        else
            level0 &= ~bit;
    }
    chunk->occupancy.level0 = level0;
    chunk->occupancy.level1 = level1_from_level0(level0);
}

void chunk_rebuild_occupancy(Chunk *chunk)
{
    /* One pass over the voxels, a region row (8 voxels) at a time */
    uint64_t level0 = 0;
    int32_t solid = 0;
    for (int32_t z = 0; z < CHUNK_SIZE; z++)
    {
        for (int32_t y = 0; y < CHUNK_SIZE; y++)
        {
            const VoxelCell *row = &chunk->voxels[chunk_voxel_index(0, y, z)];
            for (int32_t rx = 0; rx < CHUNK_MIP0_SIZE; rx++)
            {
                uint64_t w = load_row8(row + rx * 8);
                if (w == 0)
                    continue;
                level0 |= chunk_region_bit(rx * 8, y, z);

                /* Fold each byte onto its lowest bit, then add the bytes up */
                w |= w >> 4;
                w |= w >> 2;
                w |= w >> 1;
                solid += (int32_t)(((w & 0x0101010101010101ULL) * 0x0101010101010101ULL) >> 56);
            }
        }
    }

    chunk->occupancy.level0 = level0;
    chunk->occupancy.level1 = level1_from_level0(level0);
    chunk->occupancy.solid_count = (uint16_t)solid;
    chunk->occupancy.has_any = solid > 0 ? 1 : 0;
    chunk->stale_regions = 0;
}
