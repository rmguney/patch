#ifndef DATA_TERRAIN_GLSL
#define DATA_TERRAIN_GLSL

#include "hdda_types.glsl"

/* Terrain data bindings - must match descriptor set layout */
layout(std430, set = 0, binding = 0) readonly buffer TerrainVoxelBuffer {
    uint terrain_voxel_data[];
};

layout(std430, set = 0, binding = 1) readonly buffer TerrainChunkHeaders {
    uvec4 terrain_chunk_headers[];
};

/* Terrain parameters from push constants - user must define these globals:
 *   ivec3 pc_grid_size
 *   ivec3 pc_chunks_dim
 *   int pc_total_chunks
 */

/* Check if chunk has any solid voxels (level 2 hierarchy) */
bool sample_occupancy_chunk(int chunk_idx) {
    if (chunk_idx < 0 || chunk_idx >= pc_total_chunks) return false;
    return (terrain_chunk_headers[chunk_idx].z & 0xFFu) != 0u;
}

/* Get level0 occupancy bitmask for chunk */
uvec2 terrain_get_level0(int chunk_idx) {
    return terrain_chunk_headers[chunk_idx].xy;
}

/* Check if 8x8x8 region is occupied (level 1 hierarchy) */
bool sample_occupancy_region(int chunk_idx, ivec3 region) {
    uvec2 level0 = terrain_get_level0(chunk_idx);
    int bit = region.x + region.y * 4 + region.z * 16;
    if (bit < 32) {
        return (level0.x & (1u << bit)) != 0u;
    } else {
        return (level0.y & (1u << (bit - 32))) != 0u;
    }
}

/* Sample voxel material at global grid position */
uint sample_material(ivec3 p) {
    if (p.x < 0 || p.x >= pc_grid_size.x ||
        p.y < 0 || p.y >= pc_grid_size.y ||
        p.z < 0 || p.z >= pc_grid_size.z) {
        return 0u;
    }

    ivec3 chunk_pos = p / CHUNK_SIZE;
    int chunk_idx = chunk_pos.x + chunk_pos.y * pc_chunks_dim.x +
                    chunk_pos.z * pc_chunks_dim.x * pc_chunks_dim.y;

    ivec3 local = p - chunk_pos * CHUNK_SIZE;
    int local_idx = local.x + local.y * CHUNK_SIZE + local.z * CHUNK_SIZE * CHUNK_SIZE;

    int chunk_data_offset = chunk_idx * CHUNK_UINT_COUNT;
    int uint_idx = local_idx / 4;
    int byte_idx = local_idx % 4;

    uint packed = terrain_voxel_data[chunk_data_offset + uint_idx];
    return (packed >> (byte_idx * 8)) & 0xFFu;
}

#endif /* DATA_TERRAIN_GLSL */
