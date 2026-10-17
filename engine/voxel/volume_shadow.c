#include "volume.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int32_t volume_get_shadow_dirty_chunks(VoxelVolume *vol, int32_t *out_indices, int32_t max_count)
{
    if (!vol || !out_indices || max_count <= 0)
        return 0;

    /* After an overflow, hand out the lowest set bits; volume_clear_shadow_dirty
     * then clears exactly those, so the rest follow on later frames. */
    if (vol->shadow_dirty_overflow)
    {
        int32_t found = 0;
        for (int32_t i = 0; i < vol->total_chunks && found < max_count; i++)
        {
            if (vol->shadow_dirty_bitmap[i >> 6] & (1ULL << (i & 63)))
                out_indices[found++] = i;
        }
        return found;
    }

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

    if (vol->shadow_needs_full_rebuild)
    {
        /* The whole volume was packed; nothing was tracked until now */
        vol->shadow_needs_full_rebuild = false;
        return;
    }

    if (vol->shadow_dirty_overflow)
    {
        /* Clear the VOLUME_SHADOW_DIRTY_MAX chunks the renderer was handed and stay
         * in overflow mode while any remain */
        int32_t cleared = 0;
        for (int32_t i = 0; i < vol->total_chunks; i++)
        {
            uint64_t bit = 1ULL << (i & 63);
            if (!(vol->shadow_dirty_bitmap[i >> 6] & bit))
                continue;
            if (cleared == VOLUME_SHADOW_DIRTY_MAX)
                return;
            vol->shadow_dirty_bitmap[i >> 6] &= ~bit;
            cleared++;
        }
        vol->shadow_dirty_overflow = false;
    }
    else
    {
        for (int32_t i = 0; i < vol->shadow_dirty_count; i++)
        {
            int32_t chunk_idx = vol->shadow_dirty_chunks[i];
            vol->shadow_dirty_bitmap[chunk_idx >> 6] &= ~(1ULL << (chunk_idx & 63));
        }
    }
    vol->shadow_dirty_count = 0;
}

void volume_pack_shadow_chunk(const VoxelVolume *vol, const bool *occludes, int32_t chunk_idx,
                              uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0)
{
    (void)d0;
    if (!vol || !mip0 || chunk_idx < 0 || chunk_idx >= vol->total_chunks)
        return;

    const Chunk *chunk = &vol->chunks[chunk_idx];
    int32_t cx = chunk_idx % vol->chunks_x;
    int32_t cy = chunk_idx / vol->chunks_x % vol->chunks_y;
    int32_t cz = chunk_idx / (vol->chunks_x * vol->chunks_y);

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
            size_t row_start = (size_t)base_px +
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
                uint8_t material = chunk->voxels[chunk_voxel_index(lx, ly, lz)].material;
                if (material == MATERIAL_EMPTY || (occludes && !occludes[material]))
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

void volume_shadow_mip_dims(const VoxelVolume *vol, uint32_t level, uint32_t out_dims[3])
{
    const uint32_t texels = (uint32_t)VOLUME_SHADOW_CHUNK_TEXELS(level);
    out_dims[0] = (uint32_t)vol->chunks_x * texels;
    out_dims[1] = (uint32_t)vol->chunks_y * texels;
    out_dims[2] = (uint32_t)vol->chunks_z * texels;
}

static int compare_chunk_index(const void *a, const void *b)
{
    const int32_t x = *(const int32_t *)a;
    const int32_t y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

/* Position of chunk in the sorted list, -1 when it is not in it */
static int32_t find_chunk(const int32_t *sorted, int32_t count, int32_t chunk)
{
    int32_t lo = 0;
    int32_t hi = count - 1;
    while (lo <= hi)
    {
        const int32_t mid = lo + (hi - lo) / 2;
        if (sorted[mid] == chunk)
            return mid;
        if (sorted[mid] < chunk)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

/* Every chunk in [lo, hi] is in the list and in no box yet */
static bool chunk_range_free(const VoxelVolume *vol, const int32_t *sorted, const bool *in_box, int32_t count,
                             const int32_t lo[3], const int32_t hi[3])
{
    for (int32_t z = lo[2]; z <= hi[2]; z++)
        for (int32_t y = lo[1]; y <= hi[1]; y++)
            for (int32_t x = lo[0]; x <= hi[0]; x++)
            {
                const int32_t pos = find_chunk(sorted, count, volume_chunk_index(vol, x, y, z));
                /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound): find_chunk returns -1 or an index below count */
                if (pos < 0 || in_box[pos])
                    return false;
            }
    return true;
}

int32_t volume_shadow_chunk_boxes(const VoxelVolume *vol, int32_t *chunks, int32_t count, ChunkBox *out_boxes)
{
    assert(count <= VOLUME_SHADOW_DIRTY_MAX);
    if (!vol || !chunks || !out_boxes || count <= 0)
        return 0;
    if (count > VOLUME_SHADOW_DIRTY_MAX)
        count = VOLUME_SHADOW_DIRTY_MAX;

    qsort(chunks, (size_t)count, sizeof(int32_t), compare_chunk_index);
    bool in_box[VOLUME_SHADOW_DIRTY_MAX] = {false};
    const int32_t dims[3] = {vol->chunks_x, vol->chunks_y, vol->chunks_z};

    /* In index order (x fastest, then y, then z) the first chunk in no box yet is the min
     * corner of the next box */
    int32_t box_count = 0;
    for (int32_t i = 0; i < count; i++)
    {
        if (in_box[i])
            continue;

        const int32_t chunk = chunks[i];
        int32_t lo[3] = {chunk % dims[0], chunk / dims[0] % dims[1], chunk / (dims[0] * dims[1])};
        int32_t hi[3] = {lo[0], lo[1], lo[2]};
        for (int32_t axis = 0; axis < 3; axis++)
        {
            while (hi[axis] + 1 < dims[axis])
            {
                int32_t slab_lo[3] = {lo[0], lo[1], lo[2]};
                int32_t slab_hi[3] = {hi[0], hi[1], hi[2]};
                slab_lo[axis] = slab_hi[axis] = hi[axis] + 1;
                if (!chunk_range_free(vol, chunks, in_box, count, slab_lo, slab_hi))
                    break;
                hi[axis]++;
            }
        }

        for (int32_t z = lo[2]; z <= hi[2]; z++)
            for (int32_t y = lo[1]; y <= hi[1]; y++)
                for (int32_t x = lo[0]; x <= hi[0]; x++)
                    in_box[find_chunk(chunks, count, volume_chunk_index(vol, x, y, z))] = true;

        ChunkBox *box = &out_boxes[box_count++];
        box->min_x = lo[0];
        box->min_y = lo[1];
        box->min_z = lo[2];
        box->max_x = hi[0];
        box->max_y = hi[1];
        box->max_z = hi[2];
    }
    return box_count;
}

/* Texels [lo, hi) of dst from the mip below it */
static void build_shadow_mip(const uint8_t *src, const uint32_t src_dims[3], uint8_t *dst, const uint32_t dst_dims[3],
                             const uint32_t lo[3], const uint32_t hi[3])
{
    const size_t src_slice = (size_t)src_dims[0] * src_dims[1];
    for (uint32_t z = lo[2]; z < hi[2]; z++)
    {
        for (uint32_t y = lo[1]; y < hi[1]; y++)
        {
            uint8_t *row = dst + (size_t)y * dst_dims[0] + (size_t)z * dst_dims[0] * dst_dims[1];
            for (uint32_t x = lo[0]; x < hi[0]; x++)
            {
                /* Bit (x & 1) + ((y & 1) << 1) + ((z & 1) << 2) of the 2x2x2 below, as in mip0 */
                uint8_t bits = 0;
                for (uint32_t bit = 0; bit < 8; bit++)
                {
                    const size_t sx = 2 * (size_t)x + (bit & 1);
                    const size_t sy = 2 * (size_t)y + ((bit >> 1) & 1);
                    const size_t sz = 2 * (size_t)z + ((bit >> 2) & 1);
                    if (src[sx + sy * src_dims[0] + sz * src_slice] != 0)
                        bits |= (uint8_t)(1u << bit);
                }
                row[x] = bits;
            }
        }
    }
}

void volume_shadow_build_mips(const VoxelVolume *vol, const ChunkBox *box, uint8_t *const mips[VOLUME_SHADOW_MIPS])
{
    if (!vol || !box || !mips)
        return;

    for (uint32_t level = 1; level < VOLUME_SHADOW_MIPS; level++)
    {
        const uint32_t texels = (uint32_t)VOLUME_SHADOW_CHUNK_TEXELS(level);
        uint32_t src_dims[3], dst_dims[3];
        volume_shadow_mip_dims(vol, level - 1, src_dims);
        volume_shadow_mip_dims(vol, level, dst_dims);
        const uint32_t lo[3] = {(uint32_t)box->min_x * texels, (uint32_t)box->min_y * texels, (uint32_t)box->min_z * texels};
        const uint32_t hi[3] = {(uint32_t)(box->max_x + 1) * texels, (uint32_t)(box->max_y + 1) * texels,
                                (uint32_t)(box->max_z + 1) * texels};
        build_shadow_mip(mips[level - 1], src_dims, mips[level], dst_dims, lo, hi);
    }
}

size_t volume_shadow_copy_regions(const ChunkBox *boxes, int32_t box_count, ShadowCopyRegion *out_regions)
{
    size_t staging_size = 0;
    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
    {
        const uint32_t texels = (uint32_t)VOLUME_SHADOW_CHUNK_TEXELS(level);
        for (int32_t i = 0; i < box_count; i++)
        {
            const ChunkBox *box = &boxes[i];
            ShadowCopyRegion *region = &out_regions[(int32_t)level * box_count + i];
            region->level = level;
            region->offset[0] = (uint32_t)box->min_x * texels;
            region->offset[1] = (uint32_t)box->min_y * texels;
            region->offset[2] = (uint32_t)box->min_z * texels;
            region->extent[0] = (uint32_t)(box->max_x - box->min_x + 1) * texels;
            region->extent[1] = (uint32_t)(box->max_y - box->min_y + 1) * texels;
            region->extent[2] = (uint32_t)(box->max_z - box->min_z + 1) * texels;
            region->staging_offset = staging_size;
            staging_size += (size_t)region->extent[0] * region->extent[1] * region->extent[2];
        }
    }
    return staging_size;
}

void volume_shadow_stage_regions(const VoxelVolume *vol, const uint8_t *const mips[VOLUME_SHADOW_MIPS],
                                 const ShadowCopyRegion *regions, int32_t region_count, uint8_t *staging)
{
    for (int32_t i = 0; i < region_count; i++)
    {
        const ShadowCopyRegion *region = &regions[i];
        uint32_t dims[3];
        volume_shadow_mip_dims(vol, region->level, dims);
        const uint8_t *mip = mips[region->level];
        uint8_t *dst = staging + region->staging_offset;
        for (uint32_t z = 0; z < region->extent[2]; z++)
        {
            for (uint32_t y = 0; y < region->extent[1]; y++)
            {
                const size_t src = (size_t)region->offset[0] + (size_t)(region->offset[1] + y) * dims[0] +
                                   (size_t)(region->offset[2] + z) * dims[0] * dims[1];
                memcpy(dst, mip + src, region->extent[0]);
                dst += region->extent[0];
            }
        }
    }
}
