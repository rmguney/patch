#ifndef PATCH_RENDER_GPU_VOLUME_H
#define PATCH_RENDER_GPU_VOLUME_H

#include "engine/core/types.h"
#include "engine/voxel/chunk.h"
#include "engine/voxel/volume.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* GPU raymarching ABI structs. Sizes/layout must match shaders (see _Static_assert). */

/* Maximum chunks the GPU can hold */
#define GPU_VOLUME_MAX_CHUNKS 512

/* Maximum materials in palette */
#define GPU_MATERIAL_PALETTE_SIZE 256

/* Chunk data size in bytes (32³ voxels × 1 byte) */
#define GPU_CHUNK_DATA_SIZE CHUNK_VOXEL_COUNT

    /*
     * GPUVolumeInfo: Global volume parameters for ray marching.
     * Uploaded once when volume is created or resized.
     * std140 layout compatible.
     */
    typedef struct
    {
        float bounds_min[4];    /* vec4: min_x, min_y, min_z, pad */
        float bounds_max[4];    /* vec4: max_x, max_y, max_z, pad */
        float voxel_size;       /* Size of one voxel in world units */
        float chunk_world_size; /* Size of one chunk in world units */
        int32_t chunks_x;       /* Number of chunks in X */
        int32_t chunks_y;       /* Number of chunks in Y */
        int32_t chunks_z;       /* Number of chunks in Z */
        int32_t total_chunks;   /* Total chunk count */
        int32_t voxels_x;       /* Total voxels in X (chunks_x * 32) */
        int32_t voxels_y;       /* Total voxels in Y */
        int32_t voxels_z;       /* Total voxels in Z */
        int32_t pad;            /* Padding for 16-byte alignment */
    } GPUVolumeInfo;

#ifdef __cplusplus
    static_assert(sizeof(GPUVolumeInfo) == 72, "GPUVolumeInfo must be 72 bytes");
#else
_Static_assert(sizeof(GPUVolumeInfo) == 72, "GPUVolumeInfo must be 72 bytes");
#endif

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

#ifdef __cplusplus
    static_assert(sizeof(GPUChunkHeader) == 16, "GPUChunkHeader must be 16 bytes");
#else
_Static_assert(sizeof(GPUChunkHeader) == 16, "GPUChunkHeader must be 16 bytes");
#endif

    /*
     * GPUMaterialColor: Single material entry with lighting properties.
     * Stored in palette array indexed by material ID.
     * Layout: vec4 color (r,g,b,emissive), vec4 params (roughness, metallic, flags, pad)
     *
     *   vec4 color:   r, g, b, emissive
     *   vec4 params:  roughness, metallic, flags, transparency
     *   vec4 liquid:  ior, absorption_r, absorption_g, absorption_b
     */
    typedef struct
    {
        float r, g, b, emissive; /* RGB color + emissive intensity */
        float roughness;         /* Surface roughness (0=mirror, 1=matte) */
        float metallic;          /* Metallic factor (0=dielectric, 1=metal) */
        float flags;             /* Material flags as float for GPU */
        float pad;               /* Padding for alignment */
    } GPUMaterialColor;

#ifdef __cplusplus
    static_assert(sizeof(GPUMaterialColor) == 32, "GPUMaterialColor must be 32 bytes");
#else
_Static_assert(sizeof(GPUMaterialColor) == 32, "GPUMaterialColor must be 32 bytes");
#endif

    /*
     * 48 bytes = 3 vec4s. Not yet used; placeholder for future expansion.
     */
    typedef struct
    {
        float r, g, b, emissive;
        float roughness, metallic, flags, transparency;
        float ior, absorption_r, absorption_g, absorption_b;
    } GPUMaterialColorExt;

#ifdef __cplusplus
    static_assert(sizeof(GPUMaterialColorExt) == 48, "GPUMaterialColorExt must be 48 bytes");
#else
_Static_assert(sizeof(GPUMaterialColorExt) == 48, "GPUMaterialColorExt must be 48 bytes");
#endif

    /*
     * GPUMaterialPalette: Full material color palette.
     * 256 entries × 32 bytes = 8KB.
     */
    typedef struct
    {
        GPUMaterialColor colors[GPU_MATERIAL_PALETTE_SIZE];
    } GPUMaterialPalette;

#ifdef __cplusplus
    static_assert(sizeof(GPUMaterialPalette) == 8192, "GPUMaterialPalette must be 8192 bytes");
#else
_Static_assert(sizeof(GPUMaterialPalette) == 8192, "GPUMaterialPalette must be 8192 bytes");
#endif

    /*
     * Build GPUVolumeInfo from a VoxelVolume.
     */
    static inline GPUVolumeInfo gpu_volume_info_from_volume(const VoxelVolume *vol)
    {
        GPUVolumeInfo info;

        info.bounds_min[0] = vol->bounds.min_x;
        info.bounds_min[1] = vol->bounds.min_y;
        info.bounds_min[2] = vol->bounds.min_z;
        info.bounds_min[3] = 0.0f;
        info.bounds_max[0] = vol->bounds.max_x;
        info.bounds_max[1] = vol->bounds.max_y;
        info.bounds_max[2] = vol->bounds.max_z;
        info.bounds_max[3] = 0.0f;
        info.voxel_size = vol->voxel_size;
        info.chunk_world_size = vol->voxel_size * CHUNK_SIZE;
        info.chunks_x = vol->chunks_x;
        info.chunks_y = vol->chunks_y;
        info.chunks_z = vol->chunks_z;
        info.total_chunks = vol->total_chunks;
        info.voxels_x = vol->chunks_x * CHUNK_SIZE;
        info.voxels_y = vol->chunks_y * CHUNK_SIZE;
        info.voxels_z = vol->chunks_z * CHUNK_SIZE;
        info.pad = 0;

        return info;
    }

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
     * Copy chunk voxel data (material IDs only) to output buffer.
     * Returns size in bytes (always CHUNK_VOXEL_COUNT).
     */
    static inline int32_t gpu_chunk_copy_voxels(const Chunk *chunk, uint8_t *out_data)
    {
        for (int32_t i = 0; i < CHUNK_VOXEL_COUNT; i++)
        {
            out_data[i] = chunk->voxels[i].material;
        }
        return CHUNK_VOXEL_COUNT;
    }

#ifdef __cplusplus
}
#endif

#endif
