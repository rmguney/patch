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

float volume_raycast(const VoxelVolume *vol, Vec3 origin, Vec3 dir, float max_dist,
                     Vec3 *out_hit_pos, Vec3 *out_hit_normal, uint8_t *out_material)
{
    PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);

    /* DDA ray marching with occupancy-accelerated skipping */
    float inv_voxel = 1.0f / vol->voxel_size;

    /* Transform to voxel coordinates */
    Vec3 pos;
    pos.x = (origin.x - vol->bounds.min_x) * inv_voxel;
    pos.y = (origin.y - vol->bounds.min_y) * inv_voxel;
    pos.z = (origin.z - vol->bounds.min_z) * inv_voxel;

    int32_t total_voxels_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_voxels_y = vol->chunks_y * CHUNK_SIZE;
    int32_t total_voxels_z = vol->chunks_z * CHUNK_SIZE;

    /* Current voxel coordinates */
    int32_t vx = (int32_t)floorf(pos.x);
    int32_t vy = (int32_t)floorf(pos.y);
    int32_t vz = (int32_t)floorf(pos.z);

    /* Step direction */
    int32_t step_x = (dir.x >= 0) ? 1 : -1;
    int32_t step_y = (dir.y >= 0) ? 1 : -1;
    int32_t step_z = (dir.z >= 0) ? 1 : -1;

    /* Delta t for moving one voxel in each axis */
    float delta_x = (fabsf(dir.x) > 0.0001f) ? fabsf(1.0f / dir.x) : 1e10f;
    float delta_y = (fabsf(dir.y) > 0.0001f) ? fabsf(1.0f / dir.y) : 1e10f;
    float delta_z = (fabsf(dir.z) > 0.0001f) ? fabsf(1.0f / dir.z) : 1e10f;

    /* Initial t to next voxel boundary */
    float t_max_x = ((step_x > 0 ? (float)(vx + 1) : (float)vx) - pos.x) / dir.x;
    float t_max_y = ((step_y > 0 ? (float)(vy + 1) : (float)vy) - pos.y) / dir.y;
    float t_max_z = ((step_z > 0 ? (float)(vz + 1) : (float)vz) - pos.z) / dir.z;

    if (fabsf(dir.x) < 0.0001f)
        t_max_x = 1e10f;
    if (fabsf(dir.y) < 0.0001f)
        t_max_y = 1e10f;
    if (fabsf(dir.z) < 0.0001f)
        t_max_z = 1e10f;

    float t = 0.0f;
    float max_t = max_dist * inv_voxel;
    Vec3 normal = vec3_zero();

    /* Track last chunk for occupancy caching */
    int32_t last_chunk_idx = -1;
    bool chunk_has_any = false;
    uint64_t chunk_level0 = 0;

    while (t < max_t)
    {
        /* Check if we're inside volume bounds */
        if (vx >= 0 && vx < total_voxels_x &&
            vy >= 0 && vy < total_voxels_y &&
            vz >= 0 && vz < total_voxels_z)
        {
            /* Convert to chunk + local coordinates */
            int32_t cx = vx / CHUNK_SIZE;
            int32_t cy = vy / CHUNK_SIZE;
            int32_t cz = vz / CHUNK_SIZE;
            int32_t lx = vx % CHUNK_SIZE;
            int32_t ly = vy % CHUNK_SIZE;
            int32_t lz = vz % CHUNK_SIZE;

            int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;

            /* Cache chunk occupancy when entering new chunk */
            if (chunk_idx != last_chunk_idx)
            {
                last_chunk_idx = chunk_idx;
                const Chunk *chunk = &vol->chunks[chunk_idx];
                chunk_has_any = chunk->occupancy.has_any != 0;
                chunk_level0 = chunk->occupancy.level0;
            }

            /* Skip empty chunks entirely */
            if (!chunk_has_any)
            {
                /* Advance to chunk boundary */
                goto advance_voxel;
            }

            /* Check 8x8x8 region occupancy (level0) */
            int32_t rx = lx / 8;
            int32_t ry = ly / 8;
            int32_t rz = lz / 8;
            int32_t region_bit = rx + ry * CHUNK_MIP0_SIZE + rz * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;

            if (!((chunk_level0 >> region_bit) & 1))
            {
                /* Region is empty, skip to region boundary */
                goto advance_voxel;
            }

            /* Region is occupied, check actual voxel */
            uint8_t mat = chunk_get(&vol->chunks[chunk_idx], lx, ly, lz);

            if (mat != MATERIAL_EMPTY)
            {
                /* Hit! */
                float hit_dist = t * vol->voxel_size;
                if (out_hit_pos)
                {
                    out_hit_pos->x = origin.x + dir.x * hit_dist;
                    out_hit_pos->y = origin.y + dir.y * hit_dist;
                    out_hit_pos->z = origin.z + dir.z * hit_dist;
                }
                if (out_hit_normal)
                {
                    *out_hit_normal = normal;
                }
                if (out_material)
                {
                    *out_material = mat;
                }
                PROFILE_END(PROFILE_VOXEL_RAYCAST);
                return hit_dist;
            }
        }

    advance_voxel:
        /* Advance to next voxel */
        if (t_max_x < t_max_y && t_max_x < t_max_z)
        {
            t = t_max_x;
            t_max_x += delta_x;
            vx += step_x;
            normal = vec3_create((float)(-step_x), 0.0f, 0.0f);
        }
        else if (t_max_y < t_max_z)
        {
            t = t_max_y;
            t_max_y += delta_y;
            vy += step_y;
            normal = vec3_create(0.0f, (float)(-step_y), 0.0f);
        }
        else
        {
            t = t_max_z;
            t_max_z += delta_z;
            vz += step_z;
            normal = vec3_create(0.0f, 0.0f, (float)(-step_z));
        }

        /* Early exit if outside volume */
        if ((step_x > 0 && vx >= total_voxels_x) || (step_x < 0 && vx < 0) ||
            (step_y > 0 && vy >= total_voxels_y) || (step_y < 0 && vy < 0) ||
            (step_z > 0 && vz >= total_voxels_z) || (step_z < 0 && vz < 0))
        {
            break;
        }
    }

    PROFILE_END(PROFILE_VOXEL_RAYCAST);
    return -1.0f;
}

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

        /* Mark dirty for GPU upload */
        if (chunk->state == CHUNK_STATE_ACTIVE)
        {
            chunk->state = CHUNK_STATE_DIRTY;
            chunk->dirty_frame = vol->current_frame;
            volume_push_dirty_ring(vol, chunk_idx);
        }
    }

    int32_t total_edits = vol->edit_count;
    vol->edit_count = 0;
    vol->edit_touched_count = 0;

    return total_edits;
}

bool volume_ray_hits_any_occupancy(const VoxelVolume *vol, Vec3 origin, Vec3 dir, float max_dist)
{
    if (!vol || vol->total_solid_voxels == 0)
        return false;

    float chunk_world_size = vol->voxel_size * CHUNK_SIZE;
    float inv_chunk_size = 1.0f / chunk_world_size;

    /* Transform to chunk coordinates */
    float pos_x = (origin.x - vol->bounds.min_x) * inv_chunk_size;
    float pos_y = (origin.y - vol->bounds.min_y) * inv_chunk_size;
    float pos_z = (origin.z - vol->bounds.min_z) * inv_chunk_size;

    /* Current chunk coordinates */
    int32_t cx = (int32_t)floorf(pos_x);
    int32_t cy = (int32_t)floorf(pos_y);
    int32_t cz = (int32_t)floorf(pos_z);

    /* Step direction */
    int32_t step_x = (dir.x >= 0) ? 1 : -1;
    int32_t step_y = (dir.y >= 0) ? 1 : -1;
    int32_t step_z = (dir.z >= 0) ? 1 : -1;

    /* Delta t for moving one chunk in each axis */
    float delta_x = (fabsf(dir.x) > 0.0001f) ? fabsf(chunk_world_size / dir.x) : 1e10f;
    float delta_y = (fabsf(dir.y) > 0.0001f) ? fabsf(chunk_world_size / dir.y) : 1e10f;
    float delta_z = (fabsf(dir.z) > 0.0001f) ? fabsf(chunk_world_size / dir.z) : 1e10f;

    /* Initial t to next chunk boundary */
    float t_max_x = ((step_x > 0 ? (float)(cx + 1) : (float)cx) - pos_x) * chunk_world_size / dir.x;
    float t_max_y = ((step_y > 0 ? (float)(cy + 1) : (float)cy) - pos_y) * chunk_world_size / dir.y;
    float t_max_z = ((step_z > 0 ? (float)(cz + 1) : (float)cz) - pos_z) * chunk_world_size / dir.z;

    if (fabsf(dir.x) < 0.0001f)
        t_max_x = 1e10f;
    if (fabsf(dir.y) < 0.0001f)
        t_max_y = 1e10f;
    if (fabsf(dir.z) < 0.0001f)
        t_max_z = 1e10f;

    float t = 0.0f;

    /* Traverse chunks using DDA */
    while (t < max_dist)
    {
        /* Check if we're inside volume bounds */
        if (cx >= 0 && cx < vol->chunks_x &&
            cy >= 0 && cy < vol->chunks_y &&
            cz >= 0 && cz < vol->chunks_z)
        {
            int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
            if (vol->chunks[chunk_idx].occupancy.has_any)
            {
                return true;
            }
        }

        /* Advance to next chunk */
        if (t_max_x < t_max_y && t_max_x < t_max_z)
        {
            t = t_max_x;
            t_max_x += delta_x;
            cx += step_x;
        }
        else if (t_max_y < t_max_z)
        {
            t = t_max_y;
            t_max_y += delta_y;
            cy += step_y;
        }
        else
        {
            t = t_max_z;
            t_max_z += delta_z;
            cz += step_z;
        }

        /* Early exit if outside volume */
        if ((step_x > 0 && cx >= vol->chunks_x) || (step_x < 0 && cx < 0) ||
            (step_y > 0 && cy >= vol->chunks_y) || (step_y < 0 && cy < 0) ||
            (step_z > 0 && cz >= vol->chunks_z) || (step_z < 0 && cz < 0))
        {
            break;
        }
    }

    return false;
}

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
