#include "volume.h"
#include <string.h>

void volume_pack_shadow_volume(const VoxelVolume *vol, uint8_t *out_packed,
                               uint32_t *out_width, uint32_t *out_height, uint32_t *out_depth)
{
    if (!vol || !out_packed || !out_width || !out_height || !out_depth)
        return;

    int32_t total_voxels_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_voxels_y = vol->chunks_y * CHUNK_SIZE;
    int32_t total_voxels_z = vol->chunks_z * CHUNK_SIZE;

    uint32_t packed_w = (uint32_t)(total_voxels_x >> 1);
    uint32_t packed_h = (uint32_t)(total_voxels_y >> 1);
    uint32_t packed_d = (uint32_t)(total_voxels_z >> 1);

    *out_width = packed_w;
    *out_height = packed_h;
    *out_depth = packed_d;

    size_t packed_size = (size_t)packed_w * packed_h * packed_d;
    memset(out_packed, 0, packed_size);

    for (int32_t cz = 0; cz < vol->chunks_z; cz++)
    {
        for (int32_t cy = 0; cy < vol->chunks_y; cy++)
        {
            for (int32_t cx = 0; cx < vol->chunks_x; cx++)
            {
                int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                const Chunk *chunk = &vol->chunks[chunk_idx];

                if (!chunk->occupancy.has_any)
                    continue;

                int32_t base_vx = cx * CHUNK_SIZE;
                int32_t base_vy = cy * CHUNK_SIZE;
                int32_t base_vz = cz * CHUNK_SIZE;

                for (int32_t lz = 0; lz < CHUNK_SIZE; lz++)
                {
                    for (int32_t ly = 0; ly < CHUNK_SIZE; ly++)
                    {
                        for (int32_t lx = 0; lx < CHUNK_SIZE; lx++)
                        {
                            int32_t voxel_idx = chunk_voxel_index(lx, ly, lz);
                            if (chunk->voxels[voxel_idx].material == MATERIAL_EMPTY)
                                continue;

                            int32_t vx = base_vx + lx;
                            int32_t vy = base_vy + ly;
                            int32_t vz = base_vz + lz;

                            int32_t bit_idx = (vx & 1) + ((vy & 1) << 1) + ((vz & 1) << 2);
                            int32_t px = vx >> 1;
                            int32_t py = vy >> 1;
                            int32_t pz = vz >> 1;
                            size_t packed_idx = (size_t)px + (size_t)py * packed_w +
                                                (size_t)pz * packed_w * packed_h;

                            out_packed[packed_idx] |= (uint8_t)(1 << bit_idx);
                        }
                    }
                }
            }
        }
    }
}

void volume_generate_shadow_mips(const uint8_t *mip0, uint32_t w, uint32_t h, uint32_t d,
                                 uint8_t *mip1, uint8_t *mip2)
{
    if (!mip0 || !mip1 || !mip2)
        return;

    uint32_t w1 = w >> 1;
    uint32_t h1 = h >> 1;
    uint32_t d1 = d >> 1;

    if (w1 == 0)
        w1 = 1;
    if (h1 == 0)
        h1 = 1;
    if (d1 == 0)
        d1 = 1;

    memset(mip1, 0, (size_t)w1 * h1 * d1);

    for (uint32_t z = 0; z < d; z++)
    {
        for (uint32_t y = 0; y < h; y++)
        {
            for (uint32_t x = 0; x < w; x++)
            {
                size_t idx0 = (size_t)x + (size_t)y * w + (size_t)z * w * h;
                if (mip0[idx0] != 0)
                {
                    uint32_t x1 = x >> 1;
                    uint32_t y1 = y >> 1;
                    uint32_t z1 = z >> 1;

                    int32_t bit_idx = (int32_t)((x & 1) + ((y & 1) << 1) + ((z & 1) << 2));
                    size_t idx1 = (size_t)x1 + (size_t)y1 * w1 + (size_t)z1 * w1 * h1;
                    mip1[idx1] |= (uint8_t)(1 << bit_idx);
                }
            }
        }
    }

    uint32_t w2 = w1 >> 1;
    uint32_t h2 = h1 >> 1;
    uint32_t d2 = d1 >> 1;

    if (w2 == 0)
        w2 = 1;
    if (h2 == 0)
        h2 = 1;
    if (d2 == 0)
        d2 = 1;

    memset(mip2, 0, (size_t)w2 * h2 * d2);

    for (uint32_t z = 0; z < d1; z++)
    {
        for (uint32_t y = 0; y < h1; y++)
        {
            for (uint32_t x = 0; x < w1; x++)
            {
                size_t idx1 = (size_t)x + (size_t)y * w1 + (size_t)z * w1 * h1;
                if (mip1[idx1] != 0)
                {
                    uint32_t x2 = x >> 1;
                    uint32_t y2 = y >> 1;
                    uint32_t z2 = z >> 1;

                    int32_t bit_idx = (int32_t)((x & 1) + ((y & 1) << 1) + ((z & 1) << 2));
                    size_t idx2 = (size_t)x2 + (size_t)y2 * w2 + (size_t)z2 * w2 * h2;
                    mip2[idx2] |= (uint8_t)(1 << bit_idx);
                }
            }
        }
    }
}

int32_t volume_get_shadow_dirty_chunks(VoxelVolume *vol, int32_t *out_indices, int32_t max_count)
{
    if (!vol || !out_indices || max_count <= 0)
        return 0;

    int32_t count = vol->shadow_dirty_count < max_count ? vol->shadow_dirty_count : max_count;
    for (int32_t i = 0; i < count; i++)
    {
        out_indices[i] = vol->shadow_dirty_chunks[i];
    }
    return count;
}

void volume_clear_shadow_dirty(VoxelVolume *vol)
{
    if (!vol)
        return;

    for (int32_t i = 0; i < vol->shadow_dirty_count; i++)
    {
        int32_t chunk_idx = vol->shadow_dirty_chunks[i];
        int32_t word = chunk_idx >> 6;
        int32_t bit = chunk_idx & 63;
        vol->shadow_dirty_bitmap[word] &= ~(1ULL << bit);
    }
    vol->shadow_dirty_count = 0;
    vol->shadow_needs_full_rebuild = false;
}

bool volume_shadow_needs_full_rebuild(const VoxelVolume *vol)
{
    return vol && vol->shadow_needs_full_rebuild;
}

void volume_pack_shadow_chunk(const VoxelVolume *vol, int32_t chunk_idx,
                              uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0)
{
    (void)d0;
    if (!vol || !mip0 || chunk_idx < 0 || chunk_idx >= vol->total_chunks)
        return;

    const Chunk *chunk = &vol->chunks[chunk_idx];
    int32_t cx = chunk->coord_x;
    int32_t cy = chunk->coord_y;
    int32_t cz = chunk->coord_z;

    int32_t base_vx = cx * CHUNK_SIZE;
    int32_t base_vy = cy * CHUNK_SIZE;
    int32_t base_vz = cz * CHUNK_SIZE;

    int32_t base_px = base_vx >> 1;
    int32_t base_py = base_vy >> 1;
    int32_t base_pz = base_vz >> 1;
    int32_t region_size = CHUNK_SIZE >> 1;

    for (int32_t pz = 0; pz < region_size; pz++)
    {
        for (int32_t py = 0; py < region_size; py++)
        {
            size_t row_start = (size_t)(base_px) +
                               (size_t)(base_py + py) * w0 +
                               (size_t)(base_pz + pz) * w0 * h0;
            memset(&mip0[row_start], 0, (size_t)region_size);
        }
    }

    if (!chunk->occupancy.has_any)
        return;

    for (int32_t lz = 0; lz < CHUNK_SIZE; lz++)
    {
        for (int32_t ly = 0; ly < CHUNK_SIZE; ly++)
        {
            for (int32_t lx = 0; lx < CHUNK_SIZE; lx++)
            {
                int32_t voxel_idx = chunk_voxel_index(lx, ly, lz);
                if (chunk->voxels[voxel_idx].material == MATERIAL_EMPTY)
                    continue;

                int32_t vx = base_vx + lx;
                int32_t vy = base_vy + ly;
                int32_t vz = base_vz + lz;

                int32_t bit_idx = (vx & 1) + ((vy & 1) << 1) + ((vz & 1) << 2);
                int32_t px = vx >> 1;
                int32_t py = vy >> 1;
                int32_t pz = vz >> 1;
                size_t packed_idx = (size_t)px + (size_t)py * w0 + (size_t)pz * w0 * h0;

                mip0[packed_idx] |= (uint8_t)(1 << bit_idx);
            }
        }
    }
}

void volume_generate_shadow_mips_for_chunk(int32_t chunk_idx,
                                           int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                           const uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0,
                                           uint8_t *mip1, uint32_t w1, uint32_t h1, uint32_t d1,
                                           uint8_t *mip2, uint32_t w2, uint32_t h2, uint32_t d2)
{
    (void)chunks_z;
    (void)d0;
    if (!mip0 || !mip1 || !mip2)
        return;

    int32_t cx = chunk_idx % chunks_x;
    int32_t cy = (chunk_idx / chunks_x) % chunks_y;
    int32_t cz = chunk_idx / (chunks_x * chunks_y);

    int32_t base_m0_x = (cx * CHUNK_SIZE) >> 1;
    int32_t base_m0_y = (cy * CHUNK_SIZE) >> 1;
    int32_t base_m0_z = (cz * CHUNK_SIZE) >> 1;

    int32_t base_m1_x = base_m0_x >> 1;
    int32_t base_m1_y = base_m0_y >> 1;
    int32_t base_m1_z = base_m0_z >> 1;
    int32_t m1_region = CHUNK_SIZE >> 2;

    for (int32_t z = 0; z < m1_region; z++)
    {
        for (int32_t y = 0; y < m1_region; y++)
        {
            for (int32_t x = 0; x < m1_region; x++)
            {
                uint32_t m1_x = (uint32_t)(base_m1_x + x);
                uint32_t m1_y = (uint32_t)(base_m1_y + y);
                uint32_t m1_z = (uint32_t)(base_m1_z + z);

                if (m1_x >= w1 || m1_y >= h1 || m1_z >= d1)
                    continue;

                uint8_t result = 0;
                for (int bit = 0; bit < 8; bit++)
                {
                    uint32_t m0_x = (m1_x << 1) + (uint32_t)(bit & 1);
                    uint32_t m0_y = (m1_y << 1) + (uint32_t)((bit >> 1) & 1);
                    uint32_t m0_z = (m1_z << 1) + (uint32_t)((bit >> 2) & 1);

                    if (m0_x < w0 && m0_y < h0 && m0_z < d0)
                    {
                        size_t idx0 = m0_x + m0_y * w0 + m0_z * w0 * h0;
                        if (mip0[idx0] != 0)
                        {
                            result |= (uint8_t)(1 << bit);
                        }
                    }
                }

                size_t idx1 = m1_x + m1_y * w1 + m1_z * w1 * h1;
                mip1[idx1] = result;
            }
        }
    }

    int32_t base_m2_x = base_m1_x >> 1;
    int32_t base_m2_y = base_m1_y >> 1;
    int32_t base_m2_z = base_m1_z >> 1;
    int32_t m2_region = CHUNK_SIZE >> 3;
    if (m2_region < 1)
        m2_region = 1;

    for (int32_t z = 0; z < m2_region; z++)
    {
        for (int32_t y = 0; y < m2_region; y++)
        {
            for (int32_t x = 0; x < m2_region; x++)
            {
                uint32_t m2_x = (uint32_t)(base_m2_x + x);
                uint32_t m2_y = (uint32_t)(base_m2_y + y);
                uint32_t m2_z = (uint32_t)(base_m2_z + z);

                if (m2_x >= w2 || m2_y >= h2 || m2_z >= d2)
                    continue;

                uint8_t result = 0;
                for (int bit = 0; bit < 8; bit++)
                {
                    uint32_t m1_x = (m2_x << 1) + (uint32_t)(bit & 1);
                    uint32_t m1_y = (m2_y << 1) + (uint32_t)((bit >> 1) & 1);
                    uint32_t m1_z = (m2_z << 1) + (uint32_t)((bit >> 2) & 1);

                    if (m1_x < w1 && m1_y < h1 && m1_z < d1)
                    {
                        size_t idx1 = m1_x + m1_y * w1 + m1_z * w1 * h1;
                        if (mip1[idx1] != 0)
                        {
                            result |= (uint8_t)(1 << bit);
                        }
                    }
                }

                size_t idx2 = m2_x + m2_y * w2 + m2_z * w2 * h2;
                mip2[idx2] = result;
            }
        }
    }
}
