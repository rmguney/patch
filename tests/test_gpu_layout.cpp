#include "engine/render/gpu_volume.h"
#include "engine/render/gpu_chunk.h"
#include "engine/render/voxel_push_constants.h"
#include "engine/voxel/chunk.h"
#include "content/materials.h"

/* Include embedded shaders to verify link visibility */
#include "shaders_embedded.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static int test_##name()
#define RUN_TEST(name)             \
    do                             \
    {                              \
        g_tests_run++;             \
        printf("  %s... ", #name); \
        if (test_##name())         \
        {                          \
            g_tests_passed++;      \
            printf("PASS\n");      \
        }                          \
        else                       \
        {                          \
            printf("FAIL\n");      \
        }                          \
    } while (0)

#define ASSERT(cond)                              \
    do                                            \
    {                                             \
        if (!(cond))                              \
        {                                         \
            printf("ASSERT FAILED: %s\n", #cond); \
            return 0;                             \
        }                                         \
    } while (0)

/* ========================================================================= */
/* Layout Tests - Verify GPU struct sizes match shader expectations          */
/* ========================================================================= */

TEST(gpu_volume_info_size)
{
    ASSERT(sizeof(GPUVolumeInfo) == 72);
    return 1;
}

TEST(gpu_volume_info_alignment)
{
    /* std140 requires 16-byte alignment for vec4 members */
    ASSERT(offsetof(GPUVolumeInfo, bounds_min) % 16 == 0);
    ASSERT(offsetof(GPUVolumeInfo, bounds_max) % 16 == 0);
    return 1;
}

TEST(gpu_chunk_header_size)
{
    ASSERT(sizeof(GPUChunkHeader) == 16);
    return 1;
}

TEST(gpu_chunk_header_matches_uvec4)
{
    /* GPUChunkHeader must match shader's uvec4 layout */
    ASSERT(sizeof(GPUChunkHeader) == 4 * sizeof(uint32_t));
    ASSERT(offsetof(GPUChunkHeader, level0_lo) == 0);
    ASSERT(offsetof(GPUChunkHeader, level0_hi) == 4);
    ASSERT(offsetof(GPUChunkHeader, packed) == 8);
    ASSERT(offsetof(GPUChunkHeader, pad) == 12);
    return 1;
}

TEST(gpu_material_color_size)
{
    ASSERT(sizeof(GPUMaterialColor) == 32);
    return 1;
}

TEST(gpu_material_color_layout)
{
    /* Two vec4s: (r,g,b,emissive) and (roughness,metallic,flags,pad) */
    ASSERT(offsetof(GPUMaterialColor, r) == 0);
    ASSERT(offsetof(GPUMaterialColor, g) == 4);
    ASSERT(offsetof(GPUMaterialColor, b) == 8);
    ASSERT(offsetof(GPUMaterialColor, emissive) == 12);
    ASSERT(offsetof(GPUMaterialColor, roughness) == 16);
    ASSERT(offsetof(GPUMaterialColor, metallic) == 20);
    ASSERT(offsetof(GPUMaterialColor, flags) == 24);
    ASSERT(offsetof(GPUMaterialColor, pad) == 28);
    return 1;
}

TEST(gpu_material_palette_size)
{
    ASSERT(sizeof(GPUMaterialPalette) == 8192);
    ASSERT(sizeof(GPUMaterialPalette) == GPU_MATERIAL_PALETTE_SIZE * sizeof(GPUMaterialColor));
    return 1;
}

TEST(gpu_material_color_ext_size)
{
    ASSERT(sizeof(GPUMaterialColorExt) == 48);
    return 1;
}

TEST(gpu_material_color_ext_layout)
{
    ASSERT(offsetof(GPUMaterialColorExt, r) == 0);
    ASSERT(offsetof(GPUMaterialColorExt, emissive) == 12);
    ASSERT(offsetof(GPUMaterialColorExt, roughness) == 16);
    ASSERT(offsetof(GPUMaterialColorExt, transparency) == 28);
    ASSERT(offsetof(GPUMaterialColorExt, ior) == 32);
    ASSERT(offsetof(GPUMaterialColorExt, absorption_r) == 36);
    return 1;
}

TEST(voxel_instance_size)
{
    ASSERT(sizeof(VoxelInstance) == 16);
    return 1;
}

TEST(voxel_instance_layout)
{
    /* vec3 position + uint8 material + padding */
    ASSERT(offsetof(VoxelInstance, x) == 0);
    ASSERT(offsetof(VoxelInstance, y) == 4);
    ASSERT(offsetof(VoxelInstance, z) == 8);
    ASSERT(offsetof(VoxelInstance, material) == 12);
    ASSERT(offsetof(VoxelInstance, pad) == 13);
    return 1;
}

TEST(voxel_push_constants_size)
{
    ASSERT(sizeof(patch::VoxelPushConstants) == 256);
    return 1;
}

TEST(voxel_push_constants_layout)
{
    using namespace patch;
    ASSERT(offsetof(VoxelPushConstants, inv_view) == 0);
    ASSERT(offsetof(VoxelPushConstants, inv_projection) == 64);
    ASSERT(offsetof(VoxelPushConstants, bounds_min) == 128);
    ASSERT(offsetof(VoxelPushConstants, voxel_size) == 140);
    ASSERT(offsetof(VoxelPushConstants, bounds_max) == 144);
    ASSERT(offsetof(VoxelPushConstants, chunk_size) == 156);
    ASSERT(offsetof(VoxelPushConstants, camera_pos) == 160);
    ASSERT(offsetof(VoxelPushConstants, grid_size) == 176);
    ASSERT(offsetof(VoxelPushConstants, total_chunks) == 188);
    ASSERT(offsetof(VoxelPushConstants, chunks_dim) == 192);
    ASSERT(offsetof(VoxelPushConstants, frame_count) == 204);
    ASSERT(offsetof(VoxelPushConstants, rt_quality) == 208);
    ASSERT(offsetof(VoxelPushConstants, debug_mode) == 212);
    ASSERT(offsetof(VoxelPushConstants, is_orthographic) == 216);
    ASSERT(offsetof(VoxelPushConstants, max_steps) == 220);
    ASSERT(offsetof(VoxelPushConstants, near_plane) == 224);
    ASSERT(offsetof(VoxelPushConstants, far_plane) == 228);
    ASSERT(offsetof(VoxelPushConstants, object_count) == 232);
    ASSERT(offsetof(VoxelPushConstants, reserved) == 236);
    return 1;
}

TEST(voxel_temporal_ubo_size)
{
    ASSERT(sizeof(patch::VoxelTemporalUBO) == 64);
    return 1;
}

TEST(voxel_temporal_ubo_layout)
{
    using namespace patch;
    ASSERT(offsetof(VoxelTemporalUBO, prev_view_proj) == 0);
    return 1;
}

TEST(material_descriptor_size)
{
    ASSERT(sizeof(MaterialDescriptor) >= 72);
    ASSERT(sizeof(MaterialDescriptor) <= 88);
    return 1;
}

/* ========================================================================= */
/* Shader Embedding Tests - Verify shaders are linked and have valid sizes   */
/* ========================================================================= */

TEST(shader_ui_frag_embedded)
{
    using namespace patch::shaders;
    size_t size = sizeof(k_shader_ui_frag_spv);
    ASSERT(size > 0);
    ASSERT(size % 4 == 0);
    ASSERT(k_shader_ui_frag_spv[0] == 0x07230203);
    return 1;
}

TEST(shader_ui_vert_embedded)
{
    using namespace patch::shaders;
    size_t size = sizeof(k_shader_ui_vert_spv);
    ASSERT(size > 0);
    ASSERT(size % 4 == 0);
    ASSERT(k_shader_ui_vert_spv[0] == 0x07230203);
    return 1;
}

TEST(shader_voxel_vert_embedded)
{
    using namespace patch::shaders;
    size_t size = sizeof(k_shader_voxel_vert_spv);
    ASSERT(size > 0);
    ASSERT(size % 4 == 0);
    ASSERT(k_shader_voxel_vert_spv[0] == 0x07230203);
    return 1;
}

/* ========================================================================= */
/* ChunkOccupancy → GPU Header Packing Tests                                 */
/* ========================================================================= */

TEST(chunk_header_empty_chunk)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 0;
    chunk.occupancy.level0 = 0;
    chunk.occupancy.level1 = 0;
    chunk.occupancy.solid_count = 0;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    ASSERT(header.level0_lo == 0);
    ASSERT(header.level0_hi == 0);
    ASSERT((header.packed & 0xFF) == 0);        /* has_any = 0 */
    ASSERT(((header.packed >> 8) & 0xFF) == 0); /* level1 = 0 */
    ASSERT((header.packed >> 16) == 0);         /* solid_count = 0 */
    return 1;
}

TEST(chunk_header_full_occupancy)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 1;
    chunk.occupancy.level0 = 0xFFFFFFFFFFFFFFFFULL;
    chunk.occupancy.level1 = 0xFF;
    chunk.occupancy.solid_count = CHUNK_VOXEL_COUNT;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    ASSERT(header.level0_lo == 0xFFFFFFFF);
    ASSERT(header.level0_hi == 0xFFFFFFFF);
    ASSERT((header.packed & 0xFF) == 1);                /* has_any = 1 */
    ASSERT(((header.packed >> 8) & 0xFF) == 0xFF);      /* level1 = 0xFF */
    ASSERT((header.packed >> 16) == CHUNK_VOXEL_COUNT); /* solid_count */
    return 1;
}

TEST(chunk_header_partial_occupancy)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 1;
    chunk.occupancy.level0 = 0x123456789ABCDEF0ULL;
    chunk.occupancy.level1 = 0x55;
    chunk.occupancy.solid_count = 1000;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    /* Verify level0 split correctly */
    ASSERT(header.level0_lo == 0x9ABCDEF0);
    ASSERT(header.level0_hi == 0x12345678);

    /* Verify packed fields */
    ASSERT((header.packed & 0xFF) == 1);           /* has_any */
    ASSERT(((header.packed >> 8) & 0xFF) == 0x55); /* level1 */
    ASSERT((header.packed >> 16) == 1000);         /* solid_count */
    return 1;
}

TEST(chunk_header_roundtrip_level0)
{
    /* Test that level0 can be reconstructed from lo/hi */
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.level0 = 0xDEADBEEFCAFEBABEULL;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    uint64_t reconstructed = ((uint64_t)header.level0_hi << 32) | header.level0_lo;
    ASSERT(reconstructed == chunk.occupancy.level0);
    return 1;
}

TEST(gpu_volume_info_from_volume)
{
    VoxelVolume vol;
    memset(&vol, 0, sizeof(vol));
    vol.bounds.min_x = -10.0f;
    vol.bounds.min_y = -20.0f;
    vol.bounds.min_z = -30.0f;
    vol.bounds.max_x = 100.0f;
    vol.bounds.max_y = 200.0f;
    vol.bounds.max_z = 300.0f;
    vol.voxel_size = 0.25f;
    vol.chunks_x = 4;
    vol.chunks_y = 8;
    vol.chunks_z = 12;
    vol.total_chunks = 4 * 8 * 12;

    GPUVolumeInfo info = gpu_volume_info_from_volume(&vol);

    ASSERT(info.bounds_min[0] == -10.0f);
    ASSERT(info.bounds_min[1] == -20.0f);
    ASSERT(info.bounds_min[2] == -30.0f);
    ASSERT(info.bounds_max[0] == 100.0f);
    ASSERT(info.bounds_max[1] == 200.0f);
    ASSERT(info.bounds_max[2] == 300.0f);
    ASSERT(info.voxel_size == 0.25f);
    ASSERT(info.chunk_world_size == 0.25f * CHUNK_SIZE);
    ASSERT(info.chunks_x == 4);
    ASSERT(info.chunks_y == 8);
    ASSERT(info.chunks_z == 12);
    ASSERT(info.total_chunks == 384);
    ASSERT(info.voxels_x == 4 * CHUNK_SIZE);
    ASSERT(info.voxels_y == 8 * CHUNK_SIZE);
    ASSERT(info.voxels_z == 12 * CHUNK_SIZE);
    return 1;
}

/* ========================================================================= */
/* GPU Chunk Instance Building Tests                                         */
/* ========================================================================= */

TEST(gpu_chunk_build_empty)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 0;

    VoxelInstance instances[16];
    int32_t count = gpu_chunk_build_instances(&chunk, 0.0f, 0.0f, 0.0f, 1.0f, instances, 16);

    ASSERT(count == 0);
    return 1;
}

TEST(gpu_chunk_build_single_voxel)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));

    /* Place a single voxel at (0,0,0) */
    chunk_set(&chunk, 0, 0, 0, 1);

    VoxelInstance instances[16];
    int32_t count = gpu_chunk_build_instances(&chunk, 0.0f, 0.0f, 0.0f, 1.0f, instances, 16);

    ASSERT(count == 1);
    ASSERT(instances[0].material == 1);
    ASSERT(instances[0].x == 0.5f); /* center of voxel */
    ASSERT(instances[0].y == 0.5f);
    ASSERT(instances[0].z == 0.5f);
    return 1;
}

TEST(gpu_chunk_build_respects_limit)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));

    /* Fill first few voxels */
    for (int i = 0; i < 10; i++)
    {
        chunk_set(&chunk, i, 0, 0, 1);
    }

    VoxelInstance instances[5];
    int32_t count = gpu_chunk_build_instances(&chunk, 0.0f, 0.0f, 0.0f, 1.0f, instances, 5);

    ASSERT(count == 5); /* Should stop at limit */
    return 1;
}

int main()
{
    printf("=== GPU Layout Tests (Non-GPU) ===\n\n");

    printf("GPU Struct Layout:\n");
    RUN_TEST(gpu_volume_info_size);
    RUN_TEST(gpu_volume_info_alignment);
    RUN_TEST(gpu_chunk_header_size);
    RUN_TEST(gpu_chunk_header_matches_uvec4);
    RUN_TEST(gpu_material_color_size);
    RUN_TEST(gpu_material_color_layout);
    RUN_TEST(gpu_material_palette_size);
    RUN_TEST(gpu_material_color_ext_size);
    RUN_TEST(gpu_material_color_ext_layout);
    RUN_TEST(voxel_instance_size);
    RUN_TEST(voxel_instance_layout);
    RUN_TEST(voxel_push_constants_size);
    RUN_TEST(voxel_push_constants_layout);
    RUN_TEST(voxel_temporal_ubo_size);
    RUN_TEST(voxel_temporal_ubo_layout);
    RUN_TEST(material_descriptor_size);

    printf("\nShader Embedding:\n");
    RUN_TEST(shader_ui_frag_embedded);
    RUN_TEST(shader_ui_vert_embedded);
    RUN_TEST(shader_voxel_vert_embedded);

    printf("\nChunkOccupancy → GPU Packing:\n");
    RUN_TEST(chunk_header_empty_chunk);
    RUN_TEST(chunk_header_full_occupancy);
    RUN_TEST(chunk_header_partial_occupancy);
    RUN_TEST(chunk_header_roundtrip_level0);
    RUN_TEST(gpu_volume_info_from_volume);

    printf("\nGPU Chunk Instance Building:\n");
    RUN_TEST(gpu_chunk_build_empty);
    RUN_TEST(gpu_chunk_build_single_voxel);
    RUN_TEST(gpu_chunk_build_respects_limit);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
