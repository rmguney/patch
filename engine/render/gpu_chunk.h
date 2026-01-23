#ifndef PATCH_RENDER_GPU_CHUNK_H
#define PATCH_RENDER_GPU_CHUNK_H

#include "engine/voxel/chunk.h"
#include "engine/core/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum voxel instances per chunk for GPU rendering */
#define GPU_CHUNK_MAX_INSTANCES CHUNK_VOXEL_COUNT

/* Maximum chunks that can be uploaded per frame (upload budget) */
#define GPU_UPLOAD_BUDGET_PER_FRAME 16

/* Maximum total GPU chunks (mission-scale guardrail) */
#define GPU_MAX_ACTIVE_CHUNKS 512

/*
 * VoxelInstance: GPU-visible per-voxel instance data.
 * Packed for efficient GPU upload and instanced rendering.
 */
typedef struct
{
    float x, y, z;    /* World position (12 bytes) */
    uint8_t material; /* Material ID (1 byte) */
    uint8_t pad[3];   /* Padding for 16-byte alignment (3 bytes) */
} VoxelInstance;

static_assert(sizeof(VoxelInstance) == 16, "VoxelInstance must be 16 bytes for GPU alignment");

/*
 * GPUChunk: per-chunk GPU representation.
 * Contains instance data for all solid voxels in the chunk.
 */
typedef struct
{
    VoxelInstance instances[GPU_CHUNK_MAX_INSTANCES];
    int32_t instance_count;
    int32_t chunk_index; /* Source chunk index in volume */
    uint32_t upload_frame;
} GPUChunk;

/*
 * Build VoxelInstance array from a chunk.
 * Returns number of instances written.
 * Uses ChunkOccupancy for early-out on empty regions.
 */
static inline int32_t gpu_chunk_build_instances(
    const Chunk *chunk,
    float world_base_x, float world_base_y, float world_base_z,
    float voxel_size,
    VoxelInstance *out_instances,
    int32_t max_instances)
{
    int32_t count = 0;

    /* Early out if chunk is empty */
    if (!chunk->occupancy.has_any)
        return 0;

    /* Use occupancy level 0 (4x4x4 regions of 8x8x8 voxels) for hierarchical skip */
    for (int32_t mz = 0; mz < CHUNK_MIP0_SIZE; mz++)
    {
        for (int32_t my = 0; my < CHUNK_MIP0_SIZE; my++)
        {
            for (int32_t mx = 0; mx < CHUNK_MIP0_SIZE; mx++)
            {
                /* Check if this 8x8x8 region has any solid voxels */
                int32_t mip_bit = mx + my * CHUNK_MIP0_SIZE + mz * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;
                if (!((chunk->occupancy.level0 >> mip_bit) & 1))
                    continue;

                /* Region has voxels - scan the 8x8x8 block */
                int32_t base_x = mx * 8;
                int32_t base_y = my * 8;
                int32_t base_z = mz * 8;

                for (int32_t lz = 0; lz < 8 && (base_z + lz) < CHUNK_SIZE; lz++)
                {
                    for (int32_t ly = 0; ly < 8 && (base_y + ly) < CHUNK_SIZE; ly++)
                    {
                        for (int32_t lx = 0; lx < 8 && (base_x + lx) < CHUNK_SIZE; lx++)
                        {
                            int32_t vx = base_x + lx;
                            int32_t vy = base_y + ly;
                            int32_t vz = base_z + lz;

                            uint8_t mat = chunk_get(chunk, vx, vy, vz);
                            if (mat == MATERIAL_EMPTY)
                                continue;

                            if (count >= max_instances)
                                return count;

                            VoxelInstance *inst = &out_instances[count++];
                            inst->x = world_base_x + (vx + 0.5f) * voxel_size;
                            inst->y = world_base_y + (vy + 0.5f) * voxel_size;
                            inst->z = world_base_z + (vz + 0.5f) * voxel_size;
                            inst->material = mat;
                            inst->pad[0] = 0;
                            inst->pad[1] = 0;
                            inst->pad[2] = 0;
                        }
                    }
                }
            }
        }
    }

    return count;
}

#ifdef __cplusplus
}
#endif

#endif
