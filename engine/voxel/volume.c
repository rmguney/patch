#include "volume.h"
#include "engine/core/math.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

/* Bitmap helpers for O(1) chunk tracking */
static inline void bitmap_set(uint64_t *bitmap, int32_t index)
{
    bitmap[index >> 6] |= (1ULL << (index & 63));
}

static inline void bitmap_clear(uint64_t *bitmap, int32_t index)
{
    bitmap[index >> 6] &= ~(1ULL << (index & 63));
}

static inline bool bitmap_test(const uint64_t *bitmap, int32_t index)
{
    return (bitmap[index >> 6] & (1ULL << (index & 63))) != 0;
}

static inline void bitmap_clear_all(uint64_t *bitmap, int32_t word_count)
{
    memset(bitmap, 0, (size_t)word_count * sizeof(uint64_t));
}

/* Find first set bit starting from word_start, returns -1 if none found */
static inline int32_t bitmap_find_first_set(const uint64_t *bitmap, int32_t word_count, int32_t word_start)
{
    for (int32_t w = word_start; w < word_count; w++)
    {
        if (bitmap[w] != 0)
        {
#ifdef _MSC_VER
            unsigned long bit_index;
            _BitScanForward64(&bit_index, bitmap[w]);
            return w * 64 + (int32_t)bit_index;
#else
            return w * 64 + __builtin_ctzll(bitmap[w]);
#endif
        }
    }
    return -1;
}

static VoxelVolume *volume_create_internal(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                           Bounds3D bounds, float voxel_size)
{
    PROFILE_BEGIN(PROFILE_VOLUME_INIT);

    if (chunks_x <= 0 || chunks_y <= 0 || chunks_z <= 0)
    {
        PROFILE_END(PROFILE_VOLUME_INIT);
        return NULL;
    }
    if (chunks_x > VOLUME_MAX_CHUNKS_X)
        chunks_x = VOLUME_MAX_CHUNKS_X;
    if (chunks_y > VOLUME_MAX_CHUNKS_Y)
        chunks_y = VOLUME_MAX_CHUNKS_Y;
    if (chunks_z > VOLUME_MAX_CHUNKS_Z)
        chunks_z = VOLUME_MAX_CHUNKS_Z;

    VoxelVolume *vol = (VoxelVolume *)calloc(1, sizeof(VoxelVolume));
    if (!vol)
    {
        PROFILE_END(PROFILE_VOLUME_INIT);
        return NULL;
    }

    int32_t total = chunks_x * chunks_y * chunks_z;
    vol->chunks = (Chunk *)calloc((size_t)total, sizeof(Chunk));
    if (!vol->chunks)
    {
        free(vol);
        PROFILE_END(PROFILE_VOLUME_INIT);
        return NULL;
    }

    vol->chunks_x = chunks_x;
    vol->chunks_y = chunks_y;
    vol->chunks_z = chunks_z;
    vol->total_chunks = total;
    vol->bounds = bounds;
    vol->voxel_size = voxel_size;

    for (int32_t cz = 0; cz < chunks_z; cz++)
    {
        for (int32_t cy = 0; cy < chunks_y; cy++)
        {
            for (int32_t cx = 0; cx < chunks_x; cx++)
            {
                int32_t idx = cx + cy * chunks_x + cz * chunks_x * chunks_y;
                chunk_init(&vol->chunks[idx], cx, cy, cz);
                vol->chunks[idx].state = CHUNK_STATE_ACTIVE;
            }
        }
    }

    vol->dirty_count = 0;
    vol->current_frame = 0;
    vol->dirty_ring_head = 0;
    vol->dirty_ring_tail = 0;
    vol->total_solid_voxels = 0;
    vol->active_chunks = total;

    vol->shadow_dirty_count = 0;
    vol->shadow_needs_full_rebuild = true;

    PROFILE_END(PROFILE_VOLUME_INIT);
    return vol;
}

/* Push a chunk index to the dirty ring buffer (dedup: skip if chunk already dirty) */
static void volume_push_dirty_ring(VoxelVolume *vol, int32_t chunk_index)
{
    /* Always set dirty bitmap for O(1) recovery during overflow */
    bitmap_set(vol->dirty_bitmap, chunk_index);

    int32_t next_head = (vol->dirty_ring_head + 1) % VOLUME_DIRTY_RING_SIZE;
    if (next_head == vol->dirty_ring_tail)
    {
        /* Ring is full - set overflow flag for fallback scan using bitmap */
        vol->dirty_ring_overflow = true;
        return; /* Don't overwrite, bitmap scan will catch all dirty chunks */
    }
    vol->dirty_ring[vol->dirty_ring_head] = chunk_index;
    vol->dirty_ring_head = next_head;
}

VoxelVolume *volume_create(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z, Bounds3D bounds)
{
    float width = bounds.max_x - bounds.min_x;
    float height = bounds.max_y - bounds.min_y;
    float depth = bounds.max_z - bounds.min_z;

    float voxels_x = (float)(chunks_x * CHUNK_SIZE);
    float voxels_y = (float)(chunks_y * CHUNK_SIZE);
    float voxels_z = (float)(chunks_z * CHUNK_SIZE);

    float vs_x = width / voxels_x;
    float vs_y = height / voxels_y;
    float vs_z = depth / voxels_z;
    float voxel_size = (vs_x < vs_y) ? (vs_x < vs_z ? vs_x : vs_z) : (vs_y < vs_z ? vs_y : vs_z);

    return volume_create_internal(chunks_x, chunks_y, chunks_z, bounds, voxel_size);
}

VoxelVolume *volume_create_dims(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                Vec3 origin, float voxel_size)
{
    float chunk_world_size = voxel_size * CHUNK_SIZE;
    Bounds3D bounds;
    bounds.min_x = origin.x;
    bounds.min_y = origin.y;
    bounds.min_z = origin.z;
    bounds.max_x = origin.x + chunks_x * chunk_world_size;
    bounds.max_y = origin.y + chunks_y * chunk_world_size;
    bounds.max_z = origin.z + chunks_z * chunk_world_size;

    return volume_create_internal(chunks_x, chunks_y, chunks_z, bounds, voxel_size);
}

void volume_destroy(VoxelVolume *vol)
{
    if (vol)
    {
        free(vol->chunks);
        free(vol);
    }
}

void volume_clear(VoxelVolume *vol)
{
    for (int32_t i = 0; i < vol->total_chunks; i++)
    {
        chunk_fill(&vol->chunks[i], MATERIAL_EMPTY);
        vol->chunks[i].state = CHUNK_STATE_DIRTY;
    }
    vol->total_solid_voxels = 0;
}

uint8_t volume_get_at(const VoxelVolume *vol, Vec3 pos)
{
    int32_t cx, cy, cz, lx, ly, lz;
    volume_world_to_local(vol, pos, &cx, &cy, &cz, &lx, &ly, &lz);

    if (cx < 0 || cx >= vol->chunks_x ||
        cy < 0 || cy >= vol->chunks_y ||
        cz < 0 || cz >= vol->chunks_z)
    {
        return MATERIAL_EMPTY;
    }

    int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    return chunk_get(&vol->chunks[idx], lx, ly, lz);
}

void volume_set_at(VoxelVolume *vol, Vec3 pos, uint8_t material)
{
    int32_t cx, cy, cz, lx, ly, lz;
    volume_world_to_local(vol, pos, &cx, &cy, &cz, &lx, &ly, &lz);

    if (cx < 0 || cx >= vol->chunks_x ||
        cy < 0 || cy >= vol->chunks_y ||
        cz < 0 || cz >= vol->chunks_z)
    {
        return;
    }

    int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    Chunk *chunk = &vol->chunks[idx];

    uint8_t old_mat = chunk_get(chunk, lx, ly, lz);
    if (old_mat != material)
    {
        chunk_set(chunk, lx, ly, lz, material);
        chunk->dirty_frame = vol->current_frame;

        /* Ensure renderer sees this change even if chunk_set already marked DIRTY */
        volume_push_dirty_ring(vol, idx);

        if (old_mat == MATERIAL_EMPTY && material != MATERIAL_EMPTY)
        {
            vol->total_solid_voxels++;
        }
        else if (old_mat != MATERIAL_EMPTY && material == MATERIAL_EMPTY)
        {
            vol->total_solid_voxels--;
        }
    }
}

bool volume_is_solid_at(const VoxelVolume *vol, Vec3 pos)
{
    return volume_get_at(vol, pos) != MATERIAL_EMPTY;
}

int32_t volume_fill_sphere(VoxelVolume *vol, Vec3 center, float radius, uint8_t material)
{
    PROFILE_BEGIN(PROFILE_VOXEL_EDIT);

    int32_t total_modified = 0;
    float chunk_world_size = vol->voxel_size * CHUNK_SIZE;

    /* Find affected chunk range */
    int32_t cx_min = (int32_t)floorf((center.x - radius - vol->bounds.min_x) / chunk_world_size);
    int32_t cx_max = (int32_t)ceilf((center.x + radius - vol->bounds.min_x) / chunk_world_size);
    int32_t cy_min = (int32_t)floorf((center.y - radius - vol->bounds.min_y) / chunk_world_size);
    int32_t cy_max = (int32_t)ceilf((center.y + radius - vol->bounds.min_y) / chunk_world_size);
    int32_t cz_min = (int32_t)floorf((center.z - radius - vol->bounds.min_z) / chunk_world_size);
    int32_t cz_max = (int32_t)ceilf((center.z + radius - vol->bounds.min_z) / chunk_world_size);

    /* Clamp to volume bounds */
    if (cx_min < 0)
        cx_min = 0;
    if (cy_min < 0)
        cy_min = 0;
    if (cz_min < 0)
        cz_min = 0;
    if (cx_max > vol->chunks_x)
        cx_max = vol->chunks_x;
    if (cy_max > vol->chunks_y)
        cy_max = vol->chunks_y;
    if (cz_max > vol->chunks_z)
        cz_max = vol->chunks_z;

    for (int32_t cz = cz_min; cz < cz_max; cz++)
    {
        for (int32_t cy = cy_min; cy < cy_max; cy++)
        {
            for (int32_t cx = cx_min; cx < cx_max; cx++)
            {
                Chunk *chunk = volume_get_chunk(vol, cx, cy, cz);
                if (!chunk)
                    continue;

                /* Transform center to chunk-local coordinates */
                float local_cx = (center.x - vol->bounds.min_x - cx * chunk_world_size) / vol->voxel_size;
                float local_cy = (center.y - vol->bounds.min_y - cy * chunk_world_size) / vol->voxel_size;
                float local_cz = (center.z - vol->bounds.min_z - cz * chunk_world_size) / vol->voxel_size;
                float local_radius = radius / vol->voxel_size;

                int32_t old_solid = chunk->occupancy.solid_count;
                ChunkState old_state = chunk->state;
                int32_t modified = chunk_fill_sphere(chunk, local_cx, local_cy, local_cz, local_radius, material);

                if (modified > 0)
                {
                    chunk->dirty_frame = vol->current_frame;
                    int32_t new_solid = chunk->occupancy.solid_count;
                    vol->total_solid_voxels += (new_solid - old_solid);
                    total_modified += modified;

                    int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;

                    if (vol->edit_batch_active)
                    {
                        /* O(1) bitmap dedup for touched chunk tracking */
                        if (!bitmap_test(vol->edit_touched_bitmap, chunk_idx))
                        {
                            bitmap_set(vol->edit_touched_bitmap, chunk_idx);
                            if (vol->edit_touched_count < VOLUME_EDIT_BATCH_MAX_CHUNKS)
                            {
                                vol->edit_touched_chunks[vol->edit_touched_count++] = chunk_idx;
                            }
                        }

                        if (vol->edit_count < VOLUME_MAX_EDITS_PER_TICK)
                        {
                            int32_t remaining = VOLUME_MAX_EDITS_PER_TICK - vol->edit_count;
                            vol->edit_count += (modified < remaining) ? modified : remaining;
                        }
                    }

                    /* Push to dirty ring if chunk transitioned to dirty */
                    if (old_state == CHUNK_STATE_ACTIVE && chunk->state == CHUNK_STATE_DIRTY)
                    {
                        volume_push_dirty_ring(vol, chunk_idx);
                    }
                }
            }
        }
    }

    PROFILE_END(PROFILE_VOXEL_EDIT);
    return total_modified;
}

int32_t volume_fill_box(VoxelVolume *vol, Vec3 min_corner, Vec3 max_corner, uint8_t material)
{
    PROFILE_BEGIN(PROFILE_VOXEL_EDIT);

    int32_t total_modified = 0;
    float chunk_world_size = vol->voxel_size * CHUNK_SIZE;

    /* Find affected chunk range */
    int32_t cx_min = (int32_t)floorf((min_corner.x - vol->bounds.min_x) / chunk_world_size);
    int32_t cx_max = (int32_t)ceilf((max_corner.x - vol->bounds.min_x) / chunk_world_size);
    int32_t cy_min = (int32_t)floorf((min_corner.y - vol->bounds.min_y) / chunk_world_size);
    int32_t cy_max = (int32_t)ceilf((max_corner.y - vol->bounds.min_y) / chunk_world_size);
    int32_t cz_min = (int32_t)floorf((min_corner.z - vol->bounds.min_z) / chunk_world_size);
    int32_t cz_max = (int32_t)ceilf((max_corner.z - vol->bounds.min_z) / chunk_world_size);

    /* Clamp to volume bounds */
    if (cx_min < 0)
        cx_min = 0;
    if (cy_min < 0)
        cy_min = 0;
    if (cz_min < 0)
        cz_min = 0;
    if (cx_max > vol->chunks_x)
        cx_max = vol->chunks_x;
    if (cy_max > vol->chunks_y)
        cy_max = vol->chunks_y;
    if (cz_max > vol->chunks_z)
        cz_max = vol->chunks_z;

    for (int32_t cz = cz_min; cz < cz_max; cz++)
    {
        for (int32_t cy = cy_min; cy < cy_max; cy++)
        {
            for (int32_t cx = cx_min; cx < cx_max; cx++)
            {
                Chunk *chunk = volume_get_chunk(vol, cx, cy, cz);
                if (!chunk)
                    continue;

                /* Transform box to chunk-local voxel coordinates */
                float chunk_base_x = vol->bounds.min_x + cx * chunk_world_size;
                float chunk_base_y = vol->bounds.min_y + cy * chunk_world_size;
                float chunk_base_z = vol->bounds.min_z + cz * chunk_world_size;

                int32_t lx0 = (int32_t)floorf((min_corner.x - chunk_base_x) / vol->voxel_size);
                int32_t ly0 = (int32_t)floorf((min_corner.y - chunk_base_y) / vol->voxel_size);
                int32_t lz0 = (int32_t)floorf((min_corner.z - chunk_base_z) / vol->voxel_size);
                int32_t lx1 = (int32_t)ceilf((max_corner.x - chunk_base_x) / vol->voxel_size) - 1;
                int32_t ly1 = (int32_t)ceilf((max_corner.y - chunk_base_y) / vol->voxel_size) - 1;
                int32_t lz1 = (int32_t)ceilf((max_corner.z - chunk_base_z) / vol->voxel_size) - 1;

                int32_t old_solid = chunk->occupancy.solid_count;
                ChunkState old_state = chunk->state;
                int32_t modified = chunk_fill_box(chunk, lx0, ly0, lz0, lx1, ly1, lz1, material);

                if (modified > 0)
                {
                    chunk->dirty_frame = vol->current_frame;
                    int32_t new_solid = chunk->occupancy.solid_count;
                    vol->total_solid_voxels += (new_solid - old_solid);
                    total_modified += modified;

                    int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;

                    if (vol->edit_batch_active)
                    {
                        /* O(1) bitmap dedup for touched chunk tracking */
                        if (!bitmap_test(vol->edit_touched_bitmap, chunk_idx))
                        {
                            bitmap_set(vol->edit_touched_bitmap, chunk_idx);
                            if (vol->edit_touched_count < VOLUME_EDIT_BATCH_MAX_CHUNKS)
                            {
                                vol->edit_touched_chunks[vol->edit_touched_count++] = chunk_idx;
                            }
                        }

                        if (vol->edit_count < VOLUME_MAX_EDITS_PER_TICK)
                        {
                            int32_t remaining = VOLUME_MAX_EDITS_PER_TICK - vol->edit_count;
                            vol->edit_count += (modified < remaining) ? modified : remaining;
                        }
                    }

                    /* Push to dirty ring if chunk transitioned to dirty */
                    if (old_state == CHUNK_STATE_ACTIVE && chunk->state == CHUNK_STATE_DIRTY)
                    {
                        volume_push_dirty_ring(vol, chunk_idx);
                    }
                }
            }
        }
    }

    PROFILE_END(PROFILE_VOXEL_EDIT);
    return total_modified;
}

void volume_mark_chunk_dirty(VoxelVolume *vol, int32_t chunk_index)
{
    if (chunk_index < 0 || chunk_index >= vol->total_chunks)
        return;

    Chunk *chunk = &vol->chunks[chunk_index];
    if (chunk->state == CHUNK_STATE_ACTIVE)
    {
        chunk->state = CHUNK_STATE_DIRTY;
        chunk->dirty_frame = vol->current_frame;
        volume_push_dirty_ring(vol, chunk_index);
    }
}

void volume_begin_frame(VoxelVolume *vol)
{
    vol->current_frame++;
    vol->dirty_count = 0;

    /* Check if we need fallback scan due to ring overflow */
    if (vol->dirty_ring_overflow)
    {
        /* O(1) per dirty chunk using bitmap instead of O(total_chunks) scan */
        int32_t word_start = vol->dirty_bitmap_scan_pos >> 6;
        int32_t bitmap_words = (vol->total_chunks + 63) >> 6;

        while (vol->dirty_count < VOLUME_MAX_DIRTY_PER_FRAME)
        {
            int32_t chunk_idx = bitmap_find_first_set(vol->dirty_bitmap, bitmap_words, word_start);
            if (chunk_idx < 0 || chunk_idx >= vol->total_chunks)
            {
                /* No more dirty chunks in bitmap - recovery complete */
                vol->dirty_ring_overflow = false;
                vol->dirty_bitmap_scan_pos = 0;
                vol->dirty_ring_head = 0;
                vol->dirty_ring_tail = 0;
                break;
            }

            /* Verify chunk is still dirty (may have been uploaded already) */
            if (vol->chunks[chunk_idx].state == CHUNK_STATE_DIRTY)
            {
                vol->dirty_queue[vol->dirty_count].chunk_index = chunk_idx;
                vol->dirty_queue[vol->dirty_count].dirty_frame = vol->chunks[chunk_idx].dirty_frame;
                vol->dirty_count++;
            }

            /* Clear this bit and continue from next position */
            bitmap_clear(vol->dirty_bitmap, chunk_idx);
            word_start = chunk_idx >> 6;
        }

        /* Save scan position for next frame if still in overflow recovery */
        if (vol->dirty_ring_overflow && vol->dirty_count > 0)
        {
            vol->dirty_bitmap_scan_pos = vol->dirty_queue[vol->dirty_count - 1].chunk_index + 1;
        }
        return;
    }

    /* Normal path: collect dirty chunks from ring buffer (O(ring_entries)) */
    while (vol->dirty_ring_tail != vol->dirty_ring_head &&
           vol->dirty_count < VOLUME_MAX_DIRTY_PER_FRAME)
    {
        int32_t chunk_index = vol->dirty_ring[vol->dirty_ring_tail];
        vol->dirty_ring_tail = (vol->dirty_ring_tail + 1) % VOLUME_DIRTY_RING_SIZE;

        /* Verify chunk is still dirty (may have been uploaded already) */
        if (chunk_index >= 0 && chunk_index < vol->total_chunks &&
            vol->chunks[chunk_index].state == CHUNK_STATE_DIRTY)
        {
            vol->dirty_queue[vol->dirty_count].chunk_index = chunk_index;
            vol->dirty_queue[vol->dirty_count].dirty_frame = vol->chunks[chunk_index].dirty_frame;
            vol->dirty_count++;

            /* Clear bitmap bit when processing from ring (for consistency) */
            bitmap_clear(vol->dirty_bitmap, chunk_index);
        }
    }
}

int32_t volume_get_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_count)
{
    int32_t count = (vol->dirty_count < max_count) ? vol->dirty_count : max_count;
    for (int32_t i = 0; i < count; i++)
    {
        out_indices[i] = vol->dirty_queue[i].chunk_index;
    }
    return count;
}

void volume_mark_chunks_uploaded(VoxelVolume *vol, const int32_t *chunk_indices, int32_t count)
{
    for (int32_t i = 0; i < count; i++)
    {
        int32_t idx = chunk_indices[i];
        if (idx >= 0 && idx < vol->total_chunks)
        {
            if (vol->chunks[idx].state == CHUNK_STATE_DIRTY ||
                vol->chunks[idx].state == CHUNK_STATE_UPLOADING)
            {
                vol->chunks[idx].state = CHUNK_STATE_ACTIVE;
            }
        }
    }
}

void volume_rebuild_all_occupancy(VoxelVolume *vol)
{
    PROFILE_BEGIN(PROFILE_VOXEL_OCCUPANCY);

    vol->total_solid_voxels = 0;
    vol->active_chunks = 0;

    for (int32_t i = 0; i < vol->total_chunks; i++)
    {
        chunk_rebuild_occupancy(&vol->chunks[i]);
        vol->total_solid_voxels += vol->chunks[i].occupancy.solid_count;
        if (vol->chunks[i].occupancy.has_any)
        {
            vol->active_chunks++;
        }
    }

    PROFILE_END(PROFILE_VOXEL_OCCUPANCY);
}

void volume_rebuild_dirty_occupancy(VoxelVolume *vol)
{
    if (!vol)
        return;

    /* Use last_edit_chunks if available (O(touched) instead of O(total)) */
    if (vol->last_edit_count > 0)
    {
        for (int32_t i = 0; i < vol->last_edit_count; i++)
        {
            int32_t chunk_idx = vol->last_edit_chunks[i];
            if (chunk_idx >= 0 && chunk_idx < vol->total_chunks)
            {
                Chunk *chunk = &vol->chunks[chunk_idx];
                if (chunk->state == CHUNK_STATE_DIRTY)
                {
                    chunk_rebuild_occupancy(chunk);
                }
            }
        }
        return;
    }

    /* Fallback: scan dirty_queue from last begin_frame */
    if (vol->dirty_count > 0)
    {
        for (int32_t i = 0; i < vol->dirty_count; i++)
        {
            int32_t chunk_idx = vol->dirty_queue[i].chunk_index;
            if (chunk_idx >= 0 && chunk_idx < vol->total_chunks)
            {
                Chunk *chunk = &vol->chunks[chunk_idx];
                if (chunk->state == CHUNK_STATE_DIRTY)
                {
                    chunk_rebuild_occupancy(chunk);
                }
            }
        }
        return;
    }

    /* Last resort fallback: full scan (only if no tracking info available) */
    for (int32_t i = 0; i < vol->total_chunks; i++)
    {
        Chunk *chunk = &vol->chunks[i];
        if (chunk->state != CHUNK_STATE_DIRTY)
            continue;

        chunk_rebuild_occupancy(chunk);
    }
}

/* volume_raycast moved to volume_raycast.c */

/* Edit accumulator API - batches edits and rebuilds occupancy once at end */

void volume_edit_begin(VoxelVolume *vol)
{
    if (!vol || vol->edit_batch_active)
        return;

    vol->edit_batch_active = true;
    vol->edit_count = 0;
    vol->edit_touched_count = 0;

    /* Clear bitmap for O(1) dedup during this edit batch */
    bitmap_clear_all(vol->edit_touched_bitmap, VOLUME_CHUNK_BITMAP_SIZE);
}

void volume_edit_set(VoxelVolume *vol, Vec3 pos, uint8_t material)
{
    if (!vol || !vol->edit_batch_active)
        return;

    /* Enforce per-tick edit budget */
    if (vol->edit_count >= VOLUME_MAX_EDITS_PER_TICK)
        return;

    int32_t cx, cy, cz, lx, ly, lz;
    volume_world_to_local(vol, pos, &cx, &cy, &cz, &lx, &ly, &lz);

    if (cx < 0 || cx >= vol->chunks_x ||
        cy < 0 || cy >= vol->chunks_y ||
        cz < 0 || cz >= vol->chunks_z)
    {
        return;
    }

    int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    Chunk *chunk = &vol->chunks[chunk_idx];

    uint8_t old_mat = chunk_get(chunk, lx, ly, lz);
    if (old_mat == material)
        return;

    /* Perform the edit */
    chunk_set(chunk, lx, ly, lz, material);
    vol->edit_count++;

    /* Track solid voxel delta */
    if (old_mat == MATERIAL_EMPTY && material != MATERIAL_EMPTY)
    {
        vol->total_solid_voxels++;
    }
    else if (old_mat != MATERIAL_EMPTY && material == MATERIAL_EMPTY)
    {
        vol->total_solid_voxels--;
    }

    /* O(1) bitmap dedup for touched chunk tracking */
    if (!bitmap_test(vol->edit_touched_bitmap, chunk_idx))
    {
        bitmap_set(vol->edit_touched_bitmap, chunk_idx);
        if (vol->edit_touched_count < VOLUME_EDIT_BATCH_MAX_CHUNKS)
        {
            vol->edit_touched_chunks[vol->edit_touched_count++] = chunk_idx;
        }
    }
}

static void volume_mark_shadow_dirty(VoxelVolume *vol, int32_t chunk_idx)
{
    if (bitmap_test(vol->shadow_dirty_bitmap, chunk_idx))
        return;

    bitmap_set(vol->shadow_dirty_bitmap, chunk_idx);

    if (vol->shadow_dirty_count < VOLUME_SHADOW_DIRTY_MAX)
    {
        vol->shadow_dirty_chunks[vol->shadow_dirty_count++] = chunk_idx;
    }
    else
    {
        vol->shadow_needs_full_rebuild = true;
    }
}

int32_t volume_edit_end(VoxelVolume *vol)
{
    if (!vol || !vol->edit_batch_active)
        return 0;

    vol->edit_batch_active = false;

    /* Preserve touched chunks for connectivity analysis (before clearing) */
    vol->last_edit_count = vol->edit_touched_count;
    for (int32_t i = 0; i < vol->edit_touched_count; i++)
    {
        vol->last_edit_chunks[i] = vol->edit_touched_chunks[i];
    }

    /* Rebuild occupancy and mark dirty for all touched chunks */
    for (int32_t i = 0; i < vol->edit_touched_count; i++)
    {
        int32_t chunk_idx = vol->edit_touched_chunks[i];
        Chunk *chunk = &vol->chunks[chunk_idx];

        chunk_rebuild_occupancy(chunk);

        /* Mark for shadow volume update */
        volume_mark_shadow_dirty(vol, chunk_idx);

        /* Ensure GPU upload scheduling for edit batches.
           chunk_set() marks ACTIVE->DIRTY but does not set dirty_frame or enqueue. */
        if (chunk->state == CHUNK_STATE_ACTIVE)
        {
            chunk->state = CHUNK_STATE_DIRTY;
        }
        if (chunk->state == CHUNK_STATE_DIRTY)
        {
            chunk->dirty_frame = vol->current_frame;
            volume_push_dirty_ring(vol, chunk_idx);
        }
    }

    int32_t total_edits = vol->edit_count;
    vol->edit_count = 0;
    vol->edit_touched_count = 0;

    return total_edits;
}

/* volume_ray_hits_any_occupancy moved to volume_raycast.c */

/* Shadow volume functions moved to volume_shadow.c */
