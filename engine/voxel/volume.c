#include "volume.h"
#include "content/materials.h"
#include "engine/core/profile.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

static void voxel_box_reset(VoxelBox *box)
{
    box->min_x = box->min_y = box->min_z = INT32_MAX;
    box->max_x = box->max_y = box->max_z = INT32_MIN;
}

static void voxel_box_add(VoxelBox *box, int32_t gx, int32_t gy, int32_t gz)
{
    if (gx < box->min_x) box->min_x = gx;
    if (gy < box->min_y) box->min_y = gy;
    if (gz < box->min_z) box->min_z = gz;
    if (gx > box->max_x) box->max_x = gx;
    if (gy > box->max_y) box->max_y = gy;
    if (gz > box->max_z) box->max_z = gz;
}

static void volume_log_removal(VoxelVolume *vol, const VoxelBox *box)
{
    vol->removal_log[vol->removal_count % VOLUME_REMOVAL_LOG_SIZE] = *box;
    vol->removal_count++;
}

VoxelVolume *volume_create_dims(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                Vec3 origin, float voxel_size)
{
    PROFILE_BEGIN(PROFILE_VOLUME_INIT);

    if (chunks_x <= 0 || chunks_y <= 0 || chunks_z <= 0 || !(voxel_size > 0.0f))
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

    /* Zeroed chunks are empty, CHUNK_STATE_ACTIVE and have exact occupancy, so nothing
     * walks them here: their pages stay untouched until something is written */
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
    vol->voxel_size = voxel_size;
    vol->inv_voxel_size = 1.0f / voxel_size;
    vol->bounds.min_x = origin.x;
    vol->bounds.min_y = origin.y;
    vol->bounds.min_z = origin.z;
    vol->bounds.max_x = origin.x + (float)(chunks_x * CHUNK_SIZE) * voxel_size;
    vol->bounds.max_y = origin.y + (float)(chunks_y * CHUNK_SIZE) * voxel_size;
    vol->bounds.max_z = origin.z + (float)(chunks_z * CHUNK_SIZE) * voxel_size;
    vol->shadow_needs_full_rebuild = true;

    PROFILE_END(PROFILE_VOLUME_INIT);
    return vol;
}

void volume_destroy(VoxelVolume *vol)
{
    if (vol)
    {
        free(vol->chunks);
        free(vol);
    }
}

/* Queue a chunk for GPU upload. dirty_bitmap marks every queued chunk, so a chunk
 * edited many times before the next volume_begin_frame is queued (and handed to the
 * renderer) once. Once the ring overflows the bitmap alone holds the queue. */
static void volume_push_dirty_ring(VoxelVolume *vol, int32_t chunk_index)
{
    if (bitmap_test(vol->dirty_bitmap, chunk_index))
        return;
    bitmap_set(vol->dirty_bitmap, chunk_index);

    if (vol->dirty_ring_overflow)
        return;

    int32_t next_head = (vol->dirty_ring_head + 1) % VOLUME_DIRTY_RING_SIZE;
    if (next_head == vol->dirty_ring_tail)
    {
        vol->dirty_ring_overflow = true;
        return;
    }
    vol->dirty_ring[vol->dirty_ring_head] = chunk_index;
    vol->dirty_ring_head = next_head;
}

/* Dequeue a chunk into this frame's upload list (skipped if already uploaded) */
static void volume_take_dirty(VoxelVolume *vol, int32_t chunk_index)
{
    bitmap_clear(vol->dirty_bitmap, chunk_index);
    if (vol->chunks[chunk_index].state == CHUNK_STATE_DIRTY)
        vol->dirty_queue[vol->dirty_count++] = chunk_index;
}

static void volume_mark_shadow_dirty(VoxelVolume *vol, int32_t chunk_idx)
{
    /* The pending whole-volume pack covers every chunk */
    if (vol->shadow_needs_full_rebuild || bitmap_test(vol->shadow_dirty_bitmap, chunk_idx))
        return;

    bitmap_set(vol->shadow_dirty_bitmap, chunk_idx);
    if (vol->shadow_dirty_count < VOLUME_SHADOW_DIRTY_MAX)
        vol->shadow_dirty_chunks[vol->shadow_dirty_count++] = chunk_idx;
    else
        vol->shadow_dirty_overflow = true;
}

/* Account for one voxel of chunk having changed from old_mat to new_mat */
static void volume_count_voxel_change(VoxelVolume *vol, const Chunk *chunk, uint8_t old_mat, uint8_t new_mat)
{
    if (old_mat == MATERIAL_EMPTY && new_mat != MATERIAL_EMPTY)
    {
        vol->total_solid_voxels++;
        if (chunk->occupancy.solid_count == 1)
            vol->active_chunks++;
    }
    else if (old_mat != MATERIAL_EMPTY && new_mat == MATERIAL_EMPTY)
    {
        vol->total_solid_voxels--;
        if (chunk->occupancy.solid_count == 0)
            vol->active_chunks--;
    }
}

/* Add a chunk to the current edit batch (deduplicated by edit_touched_bitmap) */
static void volume_track_touched_chunk(VoxelVolume *vol, int32_t chunk_idx)
{
    if (bitmap_test(vol->edit_touched_bitmap, chunk_idx))
        return;
    bitmap_set(vol->edit_touched_bitmap, chunk_idx);
    if (vol->edit_touched_count < VOLUME_EDIT_BATCH_MAX_CHUNKS)
        vol->edit_touched_chunks[vol->edit_touched_count++] = chunk_idx;
    else
        vol->edit_touched_overflow = true;
}

/* Material of face neighbour n (0..5) of a voxel, across chunk borders, and its packed
 * position. MATERIAL_EMPTY outside the volume. */
static uint8_t volume_face_neighbor(VoxelVolume *vol, int32_t cx, int32_t cy, int32_t cz,
                                    int32_t lx, int32_t ly, int32_t lz, int32_t n, int32_t *out_packed)
{
    static const int32_t OFFSETS[6][3] = {
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

    int32_t nx = lx + OFFSETS[n][0];
    int32_t ny = ly + OFFSETS[n][1];
    int32_t nz = lz + OFFSETS[n][2];
    int32_t ncx = cx, ncy = cy, ncz = cz;

    if (nx < 0) { ncx--; nx = CHUNK_SIZE - 1; }
    else if (nx >= CHUNK_SIZE) { ncx++; nx = 0; }
    if (ny < 0) { ncy--; ny = CHUNK_SIZE - 1; }
    else if (ny >= CHUNK_SIZE) { ncy++; ny = 0; }
    if (nz < 0) { ncz--; nz = CHUNK_SIZE - 1; }
    else if (nz >= CHUNK_SIZE) { ncz++; nz = 0; }

    Chunk *neighbor_chunk = volume_get_chunk(vol, ncx, ncy, ncz);
    if (!neighbor_chunk)
        return MATERIAL_EMPTY;

    *out_packed = volume_pack_voxel_pos(ncx, ncy, ncz, nx, ny, nz);
    return chunk_get(neighbor_chunk, nx, ny, nz);
}

/* Add an analysis seed to the batch's cut boundary; false once it overflowed */
static bool volume_push_cut_boundary(VoxelVolume *vol, int32_t packed)
{
    if (vol->cut_boundary.overflow)
        return false;
    if (vol->cut_boundary.count >= VOLUME_MAX_CUT_BOUNDARY)
    {
        vol->cut_boundary.overflow = true;
        return false;
    }
    vol->cut_boundary.packed_positions[vol->cut_boundary.count++] = packed;
    return true;
}

/* A removed voxel: its solid neighbours seed the analysis */
static void volume_record_cut_boundary(VoxelVolume *vol,
                                        int32_t cx, int32_t cy, int32_t cz,
                                        int32_t lx, int32_t ly, int32_t lz)
{
    for (int32_t n = 0; n < 6; n++)
    {
        int32_t packed = 0;
        if (volume_face_neighbor(vol, cx, cy, cz, lx, ly, lz, n, &packed) != MATERIAL_EMPTY &&
            !volume_push_cut_boundary(vol, packed))
            return;
    }
}

/* A placed voxel: one touching no solid voxel may start a floating piece, so it
 * seeds the analysis itself. Returns whether it did. */
static bool volume_record_placed_voxel(VoxelVolume *vol,
                                       int32_t cx, int32_t cy, int32_t cz,
                                       int32_t lx, int32_t ly, int32_t lz)
{
    for (int32_t n = 0; n < 6; n++)
    {
        int32_t packed = 0;
        if (volume_face_neighbor(vol, cx, cy, cz, lx, ly, lz, n, &packed) != MATERIAL_EMPTY)
            return false;
    }
    volume_push_cut_boundary(vol, volume_pack_voxel_pos(cx, cy, cz, lx, ly, lz));
    return true;
}

uint8_t volume_get_at(const VoxelVolume *vol, Vec3 pos)
{
    int32_t gx, gy, gz;
    if (!volume_world_to_voxel(vol, pos, &gx, &gy, &gz))
        return MATERIAL_EMPTY;

    const Chunk *chunk = &vol->chunks[volume_voxel_chunk_index(vol, gx, gy, gz)];
    return chunk->voxels[chunk_voxel_index(gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK, gz & CHUNK_SIZE_MASK)].material;
}

void volume_set_at(VoxelVolume *vol, Vec3 pos, uint8_t material)
{
    int32_t gx, gy, gz;
    if (!volume_world_to_voxel(vol, pos, &gx, &gy, &gz))
        return;

    int32_t chunk_idx = volume_voxel_chunk_index(vol, gx, gy, gz);
    Chunk *chunk = &vol->chunks[chunk_idx];
    int32_t lx = gx & CHUNK_SIZE_MASK;
    int32_t ly = gy & CHUNK_SIZE_MASK;
    int32_t lz = gz & CHUNK_SIZE_MASK;

    uint8_t old_mat = chunk_get(chunk, lx, ly, lz);
    if (old_mat == material)
        return;

    chunk_set(chunk, lx, ly, lz, material);
    volume_count_voxel_change(vol, chunk, old_mat, material);
    volume_push_dirty_ring(vol, chunk_idx);
    volume_mark_shadow_dirty(vol, chunk_idx);

    if (material == MATERIAL_EMPTY)
    {
        VoxelBox removed = {gx, gy, gz, gx, gy, gz};
        volume_log_removal(vol, &removed);
    }
}

bool volume_is_solid_at(const VoxelVolume *vol, Vec3 pos)
{
    return material_is_solid(volume_get_at(vol, pos));
}

void volume_begin_frame(VoxelVolume *vol)
{
    vol->dirty_count = 0;

    if (vol->dirty_ring_overflow)
    {
        /* The ring overflowed: drain the bitmap instead, resuming where the last
         * frame stopped and wrapping once, so chunks queued behind the scan
         * position are found too. */
        int32_t bitmap_words = (vol->total_chunks + 63) >> 6;
        int32_t word = vol->dirty_bitmap_scan_pos >> 6;
        bool wrapped = false;

        while (vol->dirty_count < VOLUME_MAX_DIRTY_PER_FRAME)
        {
            int32_t chunk_idx = bitmap_find_first_set(vol->dirty_bitmap, bitmap_words, word);
            if (chunk_idx < 0)
            {
                if (!wrapped && word > 0)
                {
                    wrapped = true;
                    word = 0;
                    continue;
                }
                /* Queue empty; every stale ring entry was in the bitmap too */
                vol->dirty_ring_overflow = false;
                vol->dirty_bitmap_scan_pos = 0;
                vol->dirty_ring_head = 0;
                vol->dirty_ring_tail = 0;
                return;
            }

            volume_take_dirty(vol, chunk_idx);
            word = chunk_idx >> 6;
            vol->dirty_bitmap_scan_pos = chunk_idx + 1;
        }
        return;
    }

    while (vol->dirty_ring_tail != vol->dirty_ring_head &&
           vol->dirty_count < VOLUME_MAX_DIRTY_PER_FRAME)
    {
        int32_t chunk_index = vol->dirty_ring[vol->dirty_ring_tail];
        vol->dirty_ring_tail = (vol->dirty_ring_tail + 1) % VOLUME_DIRTY_RING_SIZE;
        volume_take_dirty(vol, chunk_index);
    }
}

int32_t volume_get_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_count)
{
    /* The chunks are off the queue already: any left out here would stay stale */
    assert(max_count >= vol->dirty_count);
    int32_t count = (vol->dirty_count < max_count) ? vol->dirty_count : max_count;
    for (int32_t i = 0; i < count; i++)
    {
        out_indices[i] = vol->dirty_queue[i];
    }
    return count;
}

void volume_mark_chunks_uploaded(VoxelVolume *vol, const int32_t *chunk_indices, int32_t count)
{
    for (int32_t i = 0; i < count; i++)
    {
        int32_t idx = chunk_indices[i];
        if (idx >= 0 && idx < vol->total_chunks && vol->chunks[idx].state == CHUNK_STATE_DIRTY)
            vol->chunks[idx].state = CHUNK_STATE_ACTIVE;
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

void volume_recount_solid(VoxelVolume *vol)
{
    vol->total_solid_voxels = 0;
    vol->active_chunks = 0;
    for (int32_t i = 0; i < vol->total_chunks; i++)
    {
        vol->total_solid_voxels += vol->chunks[i].occupancy.solid_count;
        vol->active_chunks += vol->chunks[i].occupancy.has_any;
    }
}

int32_t volume_column_top(const VoxelVolume *vol, int32_t gx, int32_t gz, uint8_t *out_material)
{
    return volume_column_highest(vol, gx, gz, vol->chunks_y * CHUNK_SIZE - 1, out_material);
}

int32_t volume_column_highest(const VoxelVolume *vol, int32_t gx, int32_t gz, int32_t max_gy, uint8_t *out_material)
{
    const int32_t lx = gx & CHUNK_SIZE_MASK;
    const int32_t lz = gz & CHUNK_SIZE_MASK;
    const Chunk *column = &vol->chunks[(gx >> CHUNK_SIZE_BITS) + (gz >> CHUNK_SIZE_BITS) * vol->chunks_x * vol->chunks_y];
    if (max_gy < 0)
        return -1;
    if (max_gy >= vol->chunks_y * CHUNK_SIZE)
        max_gy = vol->chunks_y * CHUNK_SIZE - 1;
    const int32_t top_cy = max_gy >> CHUNK_SIZE_BITS;
    for (int32_t cy = top_cy; cy >= 0; cy--)
    {
        const Chunk *chunk = column + cy * vol->chunks_x;
        if (!chunk->occupancy.has_any)
            continue;
        const int32_t top_ly = cy == top_cy ? (max_gy & CHUNK_SIZE_MASK) : CHUNK_SIZE - 1;
        /* Level-0 regions top down: a set bit may be stale (region emptied by an open
         * edit batch), a clear one never is */
        for (int32_t region_y = top_ly & ~7; region_y >= 0; region_y -= 8)
        {
            if (!(chunk->occupancy.level0 & chunk_region_bit(lx, region_y, lz)))
                continue;
            for (int32_t ly = region_y + 7 < top_ly ? region_y + 7 : top_ly; ly >= region_y; ly--)
            {
                uint8_t mat = chunk->voxels[chunk_voxel_index(lx, ly, lz)].material;
                if (mat != MATERIAL_EMPTY)
                {
                    if (out_material)
                        *out_material = mat;
                    return (cy << CHUNK_SIZE_BITS) + ly;
                }
            }
        }
    }
    return -1;
}

/* Edit accumulator API - batches edits and brings occupancy up to date once at the end */

void volume_edit_begin(VoxelVolume *vol)
{
    if (!vol || vol->edit_batch_active)
        return;

    /* Times the whole batch, the edits included */
    PROFILE_BEGIN(PROFILE_VOXEL_EDIT);
    vol->edit_batch_active = true;
    vol->edit_budget_bypass = false;
    vol->edit_may_detach = false;
    vol->edit_count = 0;
    vol->edit_touched_count = 0;
    vol->edit_touched_overflow = false;
    vol->cut_boundary.count = 0;
    vol->cut_boundary.overflow = false;
    voxel_box_reset(&vol->edit_removed);

    /* Clear bitmap for O(1) dedup during this edit batch */
    bitmap_clear_all(vol->edit_touched_bitmap, VOLUME_CHUNK_BITMAP_SIZE);
}

/* One voxel of the open batch, inside the volume. A new solid voxel sets its occupancy
 * bits at once; an emptied one only marks its region stale, so a region losing many
 * voxels is rescanned once, by volume_edit_end, instead of after each of them. */
static void volume_edit_voxel(VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz, uint8_t material)
{
    /* Enforce per-tick edit budget (bypassed for correctness-critical operations) */
    if (vol->edit_count >= VOLUME_MAX_EDITS_PER_TICK && !vol->edit_budget_bypass)
        return;

    int32_t cx = gx >> CHUNK_SIZE_BITS, lx = gx & CHUNK_SIZE_MASK;
    int32_t cy = gy >> CHUNK_SIZE_BITS, ly = gy & CHUNK_SIZE_MASK;
    int32_t cz = gz >> CHUNK_SIZE_BITS, lz = gz & CHUNK_SIZE_MASK;
    int32_t chunk_idx = volume_chunk_index(vol, cx, cy, cz);
    Chunk *chunk = &vol->chunks[chunk_idx];
    VoxelCell *cell = &chunk->voxels[chunk_voxel_index(lx, ly, lz)];

    uint8_t old_mat = cell->material;
    if (old_mat == material)
        return;

    cell->material = material;
    vol->edit_count++;

    if (old_mat == MATERIAL_EMPTY)
    {
        chunk->occupancy.solid_count++;
        chunk->occupancy.level0 |= chunk_region_bit(lx, ly, lz);
        chunk->occupancy.level1 |= chunk_level1_bit(lx, ly, lz);
    }
    else if (material == MATERIAL_EMPTY)
    {
        chunk->occupancy.solid_count--;
        chunk->stale_regions |= chunk_region_bit(lx, ly, lz);
        voxel_box_add(&vol->edit_removed, gx, gy, gz);
    }
    chunk->occupancy.has_any = chunk->occupancy.solid_count > 0 ? 1 : 0;
    volume_count_voxel_change(vol, chunk, old_mat, material);

    /* Seed connectivity analysis (not for island removal, which is never analysed) */
    if (!vol->edit_budget_bypass)
    {
        if (old_mat != MATERIAL_EMPTY && material == MATERIAL_EMPTY)
        {
            volume_record_cut_boundary(vol, cx, cy, cz, lx, ly, lz);
            vol->edit_may_detach = true;
        }
        else if (old_mat == MATERIAL_EMPTY && volume_record_placed_voxel(vol, cx, cy, cz, lx, ly, lz))
        {
            vol->edit_may_detach = true;
        }
    }

    volume_track_touched_chunk(vol, chunk_idx);
}

void volume_edit_set(VoxelVolume *vol, Vec3 pos, uint8_t material)
{
    int32_t gx, gy, gz;
    if (!vol || !vol->edit_batch_active || !volume_world_to_voxel(vol, pos, &gx, &gy, &gz))
        return;
    volume_edit_voxel(vol, gx, gy, gz, material);
}


static void volume_finish_edited_chunk(VoxelVolume *vol, int32_t chunk_idx)
{
    Chunk *chunk = &vol->chunks[chunk_idx];
    if (chunk->stale_regions != 0)
    {
        chunk_refresh_regions(chunk, chunk->stale_regions);
        chunk->stale_regions = 0;
    }

    volume_mark_shadow_dirty(vol, chunk_idx);

    chunk->state = CHUNK_STATE_DIRTY;
    volume_push_dirty_ring(vol, chunk_idx);
}

/* Append src's positions to dst; dst overflows when it fills or src had overflowed */
static void cut_boundary_append(CutBoundaryBuffer *dst, const CutBoundaryBuffer *src)
{
    if (dst->overflow)
        return;
    if (src->overflow || dst->count + src->count > VOLUME_MAX_CUT_BOUNDARY)
    {
        dst->overflow = true;
        return;
    }
    memcpy(dst->packed_positions + dst->count, src->packed_positions,
           (size_t)src->count * sizeof(int32_t));
    dst->count += src->count;
}

static void cut_boundary_copy(CutBoundaryBuffer *dst, const CutBoundaryBuffer *src)
{
    dst->count = 0;
    dst->overflow = false;
    cut_boundary_append(dst, src);
}

/* Add a chunk to the pending analysis set */
static void volume_add_pending_chunk(VoxelVolume *vol, int32_t chunk_idx)
{
    if (bitmap_test(vol->pending_analysis_bitmap, chunk_idx))
        return;
    bitmap_set(vol->pending_analysis_bitmap, chunk_idx);
    if (vol->pending_analysis_count < VOLUME_EDIT_BATCH_MAX_CHUNKS)
        vol->pending_analysis_chunks[vol->pending_analysis_count++] = chunk_idx;
    else
        vol->pending_analysis_overflow = true;
}

/* Add a whole chunk set (a bitmap) to the pending analysis set */
static void volume_add_pending_bitmap(VoxelVolume *vol, const uint64_t *bitmap)
{
    for (int32_t w = 0; w < VOLUME_CHUNK_BITMAP_SIZE; w++)
        vol->pending_analysis_bitmap[w] |= bitmap[w];
    vol->pending_analysis_overflow = true;
}

int32_t volume_edit_end(VoxelVolume *vol)
{
    if (!vol || !vol->edit_batch_active)
        return 0;

    vol->edit_batch_active = false;

    /* Queue the batch for connectivity analysis if it may have left something
     * floating. Island removal is skipped: a removed island was a whole component, so
     * nothing else can come loose. So are batches that only placed voxels against
     * solid ones (snow): queued, they made the analysis scan their chunks across the
     * map whenever a long dig overflowed the cut boundary. */
    if (!vol->edit_budget_bypass && vol->edit_may_detach)
    {
        cut_boundary_append(&vol->pending_cut_boundary, &vol->cut_boundary);
        for (int32_t i = 0; i < vol->edit_touched_count; i++)
            volume_add_pending_chunk(vol, vol->edit_touched_chunks[i]);
        if (vol->edit_touched_overflow)
            volume_add_pending_bitmap(vol, vol->edit_touched_bitmap);
    }

    /* Bring occupancy up to date and queue the uploads for all touched chunks. Past
     * VOLUME_EDIT_BATCH_MAX_CHUNKS the list is incomplete, so walk the bitmap
     * instead: otherwise the extra chunks never reach the GPU. */
    if (vol->edit_touched_overflow)
    {
        for (int32_t chunk_idx = 0; chunk_idx < vol->total_chunks; chunk_idx++)
        {
            if (bitmap_test(vol->edit_touched_bitmap, chunk_idx))
                volume_finish_edited_chunk(vol, chunk_idx);
        }
    }
    else
    {
        for (int32_t i = 0; i < vol->edit_touched_count; i++)
            volume_finish_edited_chunk(vol, vol->edit_touched_chunks[i]);
    }

    if (vol->edit_removed.min_x <= vol->edit_removed.max_x)
        volume_log_removal(vol, &vol->edit_removed);

    int32_t total_edits = vol->edit_count;
    vol->edit_count = 0;
    vol->edit_touched_count = 0;
    vol->edit_touched_overflow = false;

    PROFILE_END(PROFILE_VOXEL_EDIT);
    return total_edits;
}

int32_t volume_remove_labelled(VoxelVolume *vol, const VoxelBox *box, const uint16_t *labels, uint16_t label)
{
    if (!vol || !box || !labels || vol->edit_batch_active)
        return 0;

    VoxelBox clip = *box;
    if (clip.min_x < 0) clip.min_x = 0;
    if (clip.min_y < 0) clip.min_y = 0;
    if (clip.min_z < 0) clip.min_z = 0;
    if (clip.max_x >= vol->chunks_x * CHUNK_SIZE) clip.max_x = vol->chunks_x * CHUNK_SIZE - 1;
    if (clip.max_y >= vol->chunks_y * CHUNK_SIZE) clip.max_y = vol->chunks_y * CHUNK_SIZE - 1;
    if (clip.max_z >= vol->chunks_z * CHUNK_SIZE) clip.max_z = vol->chunks_z * CHUNK_SIZE - 1;
    if (clip.min_x > clip.max_x || clip.min_y > clip.max_y || clip.min_z > clip.max_z)
        return 0;

    /* A batch without budget or analysis seeds; the work is done chunk by chunk, with
     * the counters and the stale regions settled once per chunk */
    volume_edit_begin(vol);
    vol->edit_budget_bypass = true;
    int32_t removed_total = 0;
    for (int32_t cz = clip.min_z >> CHUNK_SIZE_BITS; cz <= clip.max_z >> CHUNK_SIZE_BITS; cz++)
    {
        int32_t lz0 = clip.min_z - cz * CHUNK_SIZE < 0 ? 0 : clip.min_z - cz * CHUNK_SIZE;
        int32_t lz1 = clip.max_z - cz * CHUNK_SIZE > CHUNK_SIZE_MASK ? CHUNK_SIZE_MASK : clip.max_z - cz * CHUNK_SIZE;
        for (int32_t cy = clip.min_y >> CHUNK_SIZE_BITS; cy <= clip.max_y >> CHUNK_SIZE_BITS; cy++)
        {
            int32_t ly0 = clip.min_y - cy * CHUNK_SIZE < 0 ? 0 : clip.min_y - cy * CHUNK_SIZE;
            int32_t ly1 = clip.max_y - cy * CHUNK_SIZE > CHUNK_SIZE_MASK ? CHUNK_SIZE_MASK : clip.max_y - cy * CHUNK_SIZE;
            for (int32_t cx = clip.min_x >> CHUNK_SIZE_BITS; cx <= clip.max_x >> CHUNK_SIZE_BITS; cx++)
            {
                int32_t lx0 = clip.min_x - cx * CHUNK_SIZE < 0 ? 0 : clip.min_x - cx * CHUNK_SIZE;
                int32_t lx1 = clip.max_x - cx * CHUNK_SIZE > CHUNK_SIZE_MASK ? CHUNK_SIZE_MASK : clip.max_x - cx * CHUNK_SIZE;

                int32_t chunk_idx = volume_chunk_index(vol, cx, cy, cz);
                Chunk *chunk = &vol->chunks[chunk_idx];
                if (!chunk->occupancy.has_any)
                    continue;
                const uint16_t *chunk_labels = labels + (size_t)chunk_idx * CHUNK_VOXEL_COUNT;

                int32_t removed = 0;
                uint64_t regions = 0;
                for (int32_t lz = lz0; lz <= lz1; lz++)
                {
                    for (int32_t ly = ly0; ly <= ly1; ly++)
                    {
                        int32_t row = chunk_voxel_index(0, ly, lz);
                        for (int32_t lx = lx0; lx <= lx1; lx++)
                        {
                            VoxelCell *cell = &chunk->voxels[row + lx];
                            if (chunk_labels[row + lx] != label || cell->material == MATERIAL_EMPTY)
                                continue;
                            cell->material = MATERIAL_EMPTY;
                            regions |= chunk_region_bit(lx, ly, lz);
                            removed++;
                        }
                    }
                }
                if (removed == 0)
                    continue;

                chunk->occupancy.solid_count = (uint16_t)(chunk->occupancy.solid_count - removed);
                chunk->occupancy.has_any = chunk->occupancy.solid_count > 0 ? 1 : 0;
                chunk->stale_regions |= regions;
                vol->total_solid_voxels -= removed;
                if (!chunk->occupancy.has_any)
                    vol->active_chunks--;
                vol->edit_count += removed;
                volume_track_touched_chunk(vol, chunk_idx);
                removed_total += removed;
            }
        }
    }

    /* The removed voxels lie in the box: it is their (conservative) removal record */
    if (removed_total > 0)
        vol->edit_removed = clip;
    volume_edit_end(vol);
    return removed_total;
}

void volume_consume_pending_analysis(VoxelVolume *vol)
{
    if (!vol)
        return;

    vol->last_edit_count = vol->pending_analysis_count;
    memcpy(vol->last_edit_chunks, vol->pending_analysis_chunks,
           (size_t)vol->pending_analysis_count * sizeof(int32_t));
    vol->last_edit_overflow = vol->pending_analysis_overflow;
    if (vol->pending_analysis_overflow)
        memcpy(vol->last_edit_bitmap, vol->pending_analysis_bitmap, sizeof(vol->last_edit_bitmap));
    cut_boundary_copy(&vol->last_cut_boundary, &vol->pending_cut_boundary);

    if (vol->pending_analysis_overflow)
    {
        bitmap_clear_all(vol->pending_analysis_bitmap, VOLUME_CHUNK_BITMAP_SIZE);
    }
    else
    {
        for (int32_t i = 0; i < vol->pending_analysis_count; i++)
            bitmap_clear(vol->pending_analysis_bitmap, vol->pending_analysis_chunks[i]);
    }
    vol->pending_analysis_count = 0;
    vol->pending_analysis_overflow = false;
    vol->pending_cut_boundary.count = 0;
    vol->pending_cut_boundary.overflow = false;
}

void volume_requeue_last_analysis(VoxelVolume *vol)
{
    if (!vol)
        return;

    for (int32_t i = 0; i < vol->last_edit_count; i++)
        volume_add_pending_chunk(vol, vol->last_edit_chunks[i]);
    if (vol->last_edit_overflow)
        volume_add_pending_bitmap(vol, vol->last_edit_bitmap);
    cut_boundary_append(&vol->pending_cut_boundary, &vol->last_cut_boundary);
}
