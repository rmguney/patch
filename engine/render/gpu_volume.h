#ifndef PATCH_RENDER_GPU_VOLUME_H
#define PATCH_RENDER_GPU_VOLUME_H

#include "engine/core/types.h"
#include "engine/voxel/chunk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* GPU raymarching ABI structs. Sizes/layout must match shaders (see static_assert). */

/* Maximum materials in palette */
#define GPU_MATERIAL_PALETTE_SIZE 256

/* Chunk data size in bytes (32³ voxels × 1 byte) */
#define GPU_CHUNK_DATA_SIZE CHUNK_VOXEL_COUNT

    /*
     * GPUChunkHeader: Per-chunk metadata for hierarchical traversal.
     * Contains occupancy bitmasks for skipping empty regions.
     * Stored in array indexed by chunk index.
     */
    typedef struct
    {
        /*
         * Matches shader layout: `uvec4 chunk_headers[]`.
         * .x/.y = level0 occupancy as two uint32 (low/high)
         * .z    = packed: has_any (bits 0-7), level1 (bits 8-15), solid_count (bits 16-31)
         * .w    = unused/pad
         */
        uint32_t level0_lo;
        uint32_t level0_hi;
        uint32_t packed;
        uint32_t pad;
    } GPUChunkHeader;

static_assert(sizeof(GPUChunkHeader) == 16, "GPUChunkHeader must be 16 bytes");

    /*
     * GPUMaterialColor: Single material entry with lighting properties.
     * Stored in palette array indexed by material ID.
     * Layout: vec4 color (r,g,b,emissive), vec4 params (roughness, metallic, flags, pad)
     */
    typedef struct
    {
        float r, g, b, emissive; /* RGB color + emissive intensity */
        float roughness;         /* Surface roughness (0=mirror, 1=matte) */
        float metallic;          /* Metallic factor (0=dielectric, 1=metal) */
        float flags;             /* Material flags as float for GPU */
        float pad;               /* Padding for alignment */
    } GPUMaterialColor;

static_assert(sizeof(GPUMaterialColor) == 32, "GPUMaterialColor must be 32 bytes");

    /*
     * GPUMaterialPalette: Full material color palette.
     * 256 entries × 32 bytes = 8KB.
     */
    typedef struct
    {
        GPUMaterialColor colors[GPU_MATERIAL_PALETTE_SIZE];
    } GPUMaterialPalette;

static_assert(sizeof(GPUMaterialPalette) == 8192, "GPUMaterialPalette must be 8192 bytes");

    /*
     * Build GPUChunkHeader from a Chunk.
     */
    static inline GPUChunkHeader gpu_chunk_header_from_chunk(const Chunk *chunk)
    {
        GPUChunkHeader header;
        uint64_t level0 = chunk->occupancy.level0;
        header.level0_lo = (uint32_t)(level0 & 0xFFFFFFFFu);
        header.level0_hi = (uint32_t)((level0 >> 32) & 0xFFFFFFFFu);
        header.packed = (uint32_t)chunk->occupancy.has_any |
                        ((uint32_t)chunk->occupancy.level1 << 8) |
                        ((uint32_t)chunk->occupancy.solid_count << 16);
        header.pad = 0u;
        return header;
    }

    /*
     * Copy chunk voxel data (material IDs only, CHUNK_VOXEL_COUNT bytes) to out_data.
     */
    static inline void gpu_chunk_copy_voxels(const Chunk *chunk, uint8_t *out_data)
    {
        for (int32_t i = 0; i < CHUNK_VOXEL_COUNT; i++)
        {
            out_data[i] = chunk->voxels[i].material;
        }
    }

#ifdef __cplusplus
}
#endif

#endif
