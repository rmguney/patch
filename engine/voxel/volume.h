#ifndef PATCH_VOXEL_VOLUME_H
#define PATCH_VOXEL_VOLUME_H

#include "engine/voxel/chunk.h"
#include "engine/core/types.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define VOLUME_MAX_CHUNKS_X 64
#define VOLUME_MAX_CHUNKS_Y 32
#define VOLUME_MAX_CHUNKS_Z 64
#define VOLUME_MAX_CHUNKS (VOLUME_MAX_CHUNKS_X * VOLUME_MAX_CHUNKS_Y * VOLUME_MAX_CHUNKS_Z)

#define VOLUME_MAX_DIRTY_PER_FRAME 16
#define VOLUME_MAX_EDITS_PER_TICK 4096
/* volume_begin_frame takes chunks off the upload queue for good: the renderer must be
 * able to upload all of them, or the rest would stay stale until edited again */
#define VOLUME_MAX_UPLOADS_PER_FRAME VOLUME_MAX_DIRTY_PER_FRAME

#define VOLUME_DIRTY_RING_SIZE 64
#define VOLUME_EDIT_BATCH_MAX_CHUNKS 64
#define VOLUME_CHUNK_BITMAP_SIZE ((VOLUME_MAX_CHUNKS + 63) / 64)
#define VOLUME_SHADOW_DIRTY_MAX 256
#define VOLUME_MAX_CUT_BOUNDARY 16384
#define VOLUME_REMOVAL_LOG_SIZE 64

    typedef struct
    {
        int32_t packed_positions[VOLUME_MAX_CUT_BOUNDARY];
        int32_t count;
        bool overflow;
    } CutBoundaryBuffer;

    /* Box of volume voxel coordinates, bounds inclusive */
    typedef struct
    {
        int32_t min_x, min_y, min_z;
        int32_t max_x, max_y, max_z;
    } VoxelBox;

    typedef struct
    {
        Chunk *chunks;
        int32_t chunks_x, chunks_y, chunks_z;
        int32_t total_chunks;

        Bounds3D bounds;
        float voxel_size;
        float inv_voxel_size;

        int32_t dirty_queue[VOLUME_MAX_DIRTY_PER_FRAME]; /* Chunks to upload this frame */
        int32_t dirty_count;

        /* Upload queue: ring of chunk indices, plus dirty_bitmap marking every queued
         * chunk (it alone holds the queue after the ring overflows) */
        int32_t dirty_ring[VOLUME_DIRTY_RING_SIZE];
        int32_t dirty_ring_head;
        int32_t dirty_ring_tail;
        bool dirty_ring_overflow;

        /* Current edit batch. Chunk lists are backed by a bitmap: past
         * VOLUME_EDIT_BATCH_MAX_CHUNKS the list is incomplete (overflow) and the bitmap
         * is the full set. */
        int32_t edit_touched_chunks[VOLUME_EDIT_BATCH_MAX_CHUNKS];
        int32_t edit_touched_count;
        bool edit_touched_overflow;
        uint64_t edit_touched_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        int32_t edit_count;
        bool edit_batch_active;
        /* Batch removes whole detached islands (connectivity_remove_island): no edit
         * budget, and nothing to re-analyse since no other component can change */
        bool edit_budget_bypass;
        /* The batch removed voxels, or placed one touching no solid voxel: something
         * may float now, so volume_edit_end queues it for connectivity analysis. Voxels
         * placed against solid ones (snow on the ground) only join a component. */
        bool edit_may_detach;
        /* Seeds for the analysis: the solid neighbours of removed voxels, and placed
         * voxels touching no solid one. Once it overflows the analysis scans the
         * edited chunks instead. */
        CutBoundaryBuffer cut_boundary;
        /* Bounding box of the voxels the batch emptied (min_x > max_x: none) */
        VoxelBox edit_removed;

        /* Edits since the last connectivity analysis, accumulated by volume_edit_end */
        int32_t pending_analysis_chunks[VOLUME_EDIT_BATCH_MAX_CHUNKS];
        int32_t pending_analysis_count;
        bool pending_analysis_overflow;
        uint64_t pending_analysis_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        CutBoundaryBuffer pending_cut_boundary;

        /* The pending edits as handed to the current analysis by
         * volume_consume_pending_analysis */
        int32_t last_edit_chunks[VOLUME_EDIT_BATCH_MAX_CHUNKS];
        int32_t last_edit_count;
        bool last_edit_overflow;
        uint64_t last_edit_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        CutBoundaryBuffer last_cut_boundary;

        uint64_t dirty_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        int32_t dirty_bitmap_scan_pos;

        /* Solid voxels, and chunks holding any, as kept by the volume_* edit functions
         * (code writing chunks directly calls volume_rebuild_all_occupancy, or
         * volume_recount_solid when its writes kept the chunks' occupancy exact) */
        int32_t total_solid_voxels;
        int32_t active_chunks;

        /* Shadow volume update set. A fresh volume is packed whole by the renderer, so
         * nothing is tracked until volume_clear_shadow_dirty reports that pack done
         * (shadow_needs_full_rebuild). Past VOLUME_SHADOW_DIRTY_MAX chunks the list is
         * incomplete and the bitmap is the set (shadow_dirty_overflow). */
        uint64_t shadow_dirty_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        int32_t shadow_dirty_chunks[VOLUME_SHADOW_DIRTY_MAX];
        int32_t shadow_dirty_count;
        bool shadow_dirty_overflow;
        bool shadow_needs_full_rebuild;

        /* Where voxels were emptied, for whatever rests on them: every edit batch and
         * volume_set_at that emptied voxels appends their bounding box, entry i at
         * i % VOLUME_REMOVAL_LOG_SIZE. Readers keep their own count of the entries they
         * handled; one more than VOLUME_REMOVAL_LOG_SIZE behind has lost entries. */
        VoxelBox removal_log[VOLUME_REMOVAL_LOG_SIZE];
        uint32_t removal_count;
    } VoxelVolume;

    /* Counts above the VOLUME_MAX_CHUNKS_* limits are clamped; bounds follow the clamped
     * counts. NULL for non-positive counts or voxel size. */
    VoxelVolume *volume_create_dims(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                    Vec3 origin, float voxel_size);

    void volume_destroy(VoxelVolume *vol);

    /* Volume voxel coordinates of the voxel holding pos; false outside the volume or for a
     * NaN coordinate. Chunk and local coordinates must come from these (g >> 5, g & 31):
     * computed separately, two differently rounded divisions put points within an ulp of
     * a chunk border at local -1. */
    static inline bool volume_world_to_voxel(const VoxelVolume *vol, Vec3 pos,
                                             int32_t *gx, int32_t *gy, int32_t *gz)
    {
        float fx = floorf((pos.x - vol->bounds.min_x) * vol->inv_voxel_size);
        float fy = floorf((pos.y - vol->bounds.min_y) * vol->inv_voxel_size);
        float fz = floorf((pos.z - vol->bounds.min_z) * vol->inv_voxel_size);
        if (!(fx >= 0.0f && fx < (float)(vol->chunks_x * CHUNK_SIZE) &&
              fy >= 0.0f && fy < (float)(vol->chunks_y * CHUNK_SIZE) &&
              fz >= 0.0f && fz < (float)(vol->chunks_z * CHUNK_SIZE)))
            return false;
        *gx = (int32_t)fx;
        *gy = (int32_t)fy;
        *gz = (int32_t)fz;
        return true;
    }

    /* World centre of volume voxel (gx, gy, gz) */
    static inline Vec3 volume_voxel_to_world(const VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz)
    {
        Vec3 pos;
        pos.x = vol->bounds.min_x + ((float)gx + 0.5f) * vol->voxel_size;
        pos.y = vol->bounds.min_y + ((float)gy + 0.5f) * vol->voxel_size;
        pos.z = vol->bounds.min_z + ((float)gz + 0.5f) * vol->voxel_size;
        return pos;
    }

    static inline int32_t volume_chunk_index(const VoxelVolume *vol, int32_t cx, int32_t cy, int32_t cz)
    {
        return cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    }

    /* Chunk holding volume voxel (gx, gy, gz), which must lie inside the volume */
    static inline int32_t volume_voxel_chunk_index(const VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz)
    {
        return volume_chunk_index(vol, gx >> CHUNK_SIZE_BITS, gy >> CHUNK_SIZE_BITS, gz >> CHUNK_SIZE_BITS);
    }

    /* Index of volume voxel (gx, gy, gz) in chunk-major order (chunk index *
     * CHUNK_VOXEL_COUNT + index in the chunk), as per-voxel arrays such as
     * ConnectivityWorkBuffer's are laid out. It passes INT32_MAX from 65536 chunks on. */
    static inline size_t volume_voxel_linear_index(const VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz)
    {
        return (size_t)volume_voxel_chunk_index(vol, gx, gy, gz) * CHUNK_VOXEL_COUNT +
               (size_t)chunk_voxel_index(gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK, gz & CHUNK_SIZE_MASK);
    }

    static inline Chunk *volume_get_chunk(VoxelVolume *vol, int32_t cx, int32_t cy, int32_t cz)
    {
        if (cx < 0 || cx >= vol->chunks_x ||
            cy < 0 || cy >= vol->chunks_y ||
            cz < 0 || cz >= vol->chunks_z)
        {
            return NULL;
        }
        return &vol->chunks[volume_chunk_index(vol, cx, cy, cz)];
    }

    /* Packed voxel position used by the cut-boundary buffers: chunk coordinates in
     * 6/5/6 bits (bits 26-31, 21-25, 15-20), local coordinates in 5 bits each */
    STATIC_ASSERT(VOLUME_MAX_CHUNKS_X <= 64, "chunk x must fit volume_pack_voxel_pos");
    STATIC_ASSERT(VOLUME_MAX_CHUNKS_Y <= 32, "chunk y must fit volume_pack_voxel_pos");
    STATIC_ASSERT(VOLUME_MAX_CHUNKS_Z <= 64, "chunk z must fit volume_pack_voxel_pos");

    static inline int32_t volume_pack_voxel_pos(int32_t cx, int32_t cy, int32_t cz,
                                                int32_t lx, int32_t ly, int32_t lz)
    {
        uint32_t packed = ((uint32_t)cx << 26) | ((uint32_t)cy << 21) | ((uint32_t)cz << 15) |
                          ((uint32_t)lx << 10) | ((uint32_t)ly << 5) | (uint32_t)lz;
        return (int32_t)packed;
    }

    static inline void volume_unpack_voxel_pos(int32_t packed,
                                               int32_t *cx, int32_t *cy, int32_t *cz,
                                               int32_t *lx, int32_t *ly, int32_t *lz)
    {
        uint32_t p = (uint32_t)packed;
        *cx = (int32_t)((p >> 26) & 0x3F);
        *cy = (int32_t)((p >> 21) & 0x1F);
        *cz = (int32_t)((p >> 15) & 0x3F);
        *lx = (int32_t)((p >> 10) & 0x1F);
        *ly = (int32_t)((p >> 5) & 0x1F);
        *lz = (int32_t)(p & 0x1F);
    }

    uint8_t volume_get_at(const VoxelVolume *vol, Vec3 pos);
    /* One voxel outside an edit batch: occupancy, counters, GPU upload and shadow
     * queues are kept, but nothing is queued for connectivity analysis */
    void volume_set_at(VoxelVolume *vol, Vec3 pos, uint8_t material);
    /* The voxel at pos is of a MAT_FLAG_SOLID material: bodies and debris collide with it
     * (not with air, water or ground flora) */
    bool volume_is_solid_at(const VoxelVolume *vol, Vec3 pos);
    void volume_begin_frame(VoxelVolume *vol);
    /* The chunks volume_begin_frame took off the queue; max_count must hold them all */
    int32_t volume_get_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_count);
    void volume_mark_chunks_uploaded(VoxelVolume *vol, const int32_t *chunk_indices, int32_t count);
    void volume_rebuild_all_occupancy(VoxelVolume *vol);
    /* Recomputes the volume's solid counters from the chunks' occupancy without reading a
     * voxel: for chunks written through chunk_set, which keeps their occupancy exact (world
     * generation), where a rebuild would rescan, and commit, every page of the volume */
    void volume_recount_solid(VoxelVolume *vol);
    /* Top solid voxel of column (gx, gz), and its material, or -1 for an empty column.
     * Empty chunks and empty 8x8x8 regions are skipped by their occupancy; the chunk
     * headers of a column are shared by its whole chunk column, so scans visiting the
     * columns one chunk column at a time keep them in cache. */
    int32_t volume_column_top(const VoxelVolume *vol, int32_t gx, int32_t gz, uint8_t *out_material);
    /* The same for the part of the column at or below voxel layer max_gy (-1 when it is
     * empty there, or max_gy < 0) */
    int32_t volume_column_highest(const VoxelVolume *vol, int32_t gx, int32_t gz, int32_t max_gy, uint8_t *out_material);
    /* Distance to the first solid voxel along unit dir within max_dist, or -1 on a
     * miss; materials with any of skip_flags set are passed through */
    float volume_raycast(const VoxelVolume *vol, Vec3 origin, Vec3 dir, float max_dist,
                         Vec3 *out_hit_pos, Vec3 *out_hit_normal, uint8_t *out_material,
                         uint32_t skip_flags);

    /* Edit batches: voxels change at once, occupancy is brought up to date, chunks are
     * queued for upload and the edit for connectivity analysis at volume_edit_end */
    void volume_edit_begin(VoxelVolume *vol);
    void volume_edit_set(VoxelVolume *vol, Vec3 pos, uint8_t material);
    int32_t volume_edit_end(VoxelVolume *vol);

    /* Empties, as an edit batch of its own (not inside another), every voxel in box whose
     * label is `label`. labels holds one entry per voxel in volume_voxel_linear_index
     * order, as ConnectivityWorkBuffer.island_ids does. Removing a whole component frees
     * nothing else, so the batch has no edit budget and is not queued for analysis.
     * Returns the voxels removed. */
    int32_t volume_remove_labelled(VoxelVolume *vol, const VoxelBox *box, const uint16_t *labels, uint16_t label);
    /* Hand the pending edits to analysis (last_*) and start a new pending set */
    void volume_consume_pending_analysis(VoxelVolume *vol);
    /* Put the edits last handed to analysis back into the pending set, for an
     * analysis that could not finish. */
    void volume_requeue_last_analysis(VoxelVolume *vol);

    int32_t volume_get_shadow_dirty_chunks(VoxelVolume *vol, int32_t *out_indices, int32_t max_count);
    /* The renderer packed the chunks it was handed (or, the first time, the whole volume) */
    void volume_clear_shadow_dirty(VoxelVolume *vol);

    /* Shadow volume packing (1 bit per voxel, 2x2x2 voxels per byte) of one chunk's texels of
     * mip0 (dimensions w0 x h0 x d0). occludes[material] says whether a voxel blocks light
     * (VOXEL_MATERIAL_MAX entries); NULL treats every non-empty voxel as opaque. Chunks write
     * disjoint texels: a whole volume packs chunk by chunk, in parallel. */
    void volume_pack_shadow_chunk(const VoxelVolume *vol, const bool *occludes, int32_t chunk_idx,
                                  uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0);

/*
 * The shadow volume's mips: mip0 is the packed volume, each texel of mips 1 to 4 has one bit per
 * non-empty texel of the 2x2x2 below it, so a texel of mip `level` covers a block of
 * 2^(level + 1) voxels per axis, up to a whole chunk in mip 4. A chunk covers
 * VOLUME_SHADOW_CHUNK_TEXELS(level) texels per axis of mip `level` (16, 8, 4, 2, 1): every mip
 * texel lies in one chunk, so the mips of a set of chunks depend on those chunks alone, and
 * their texels are what an edit changes.
 */
#define VOLUME_SHADOW_MIPS 5
#define VOLUME_SHADOW_CHUNK_TEXELS(level) (CHUNK_SIZE >> ((level) + 1))
#define VOLUME_SHADOW_CHUNK_MIP_BYTES(level) \
    (VOLUME_SHADOW_CHUNK_TEXELS(level) * VOLUME_SHADOW_CHUNK_TEXELS(level) * VOLUME_SHADOW_CHUNK_TEXELS(level))
/* Texels of one chunk over all mips */
#define VOLUME_SHADOW_CHUNK_BYTES (VOLUME_SHADOW_CHUNK_MIP_BYTES(0) + VOLUME_SHADOW_CHUNK_MIP_BYTES(1) + \
                                   VOLUME_SHADOW_CHUNK_MIP_BYTES(2) + VOLUME_SHADOW_CHUNK_MIP_BYTES(3) + \
                                   VOLUME_SHADOW_CHUNK_MIP_BYTES(4))
    STATIC_ASSERT(VOLUME_SHADOW_MIPS == 5, "VOLUME_SHADOW_CHUNK_BYTES sums every mip");
    STATIC_ASSERT(VOLUME_SHADOW_CHUNK_TEXELS(VOLUME_SHADOW_MIPS - 1) == 1, "the coarsest mip has a texel per chunk");

    /* Box of chunk coordinates, bounds inclusive */
    typedef struct
    {
        int32_t min_x, min_y, min_z;
        int32_t max_x, max_y, max_z;
    } ChunkBox;

    /* One box of one mip as uploaded: texel offset and extent within the mip, and where its
     * texels start in the staging data (tightly packed rows, x fastest, then y, then z) */
    typedef struct
    {
        uint32_t level;
        uint32_t offset[3];
        uint32_t extent[3];
        size_t staging_offset;
    } ShadowCopyRegion;

    /* Width, height and depth of shadow mip `level` */
    void volume_shadow_mip_dims(const VoxelVolume *vol, uint32_t level, uint32_t out_dims[3]);

    /* Covers the given distinct chunks (at most VOLUME_SHADOW_DIRTY_MAX, any order) exactly
     * with disjoint boxes: runs along x, grown over y and then z while the next row or layer
     * is all in the set. Sorts chunks. Returns the number of boxes (at most count). */
    int32_t volume_shadow_chunk_boxes(const VoxelVolume *vol, int32_t *chunks, int32_t count, ChunkBox *out_boxes);

    /* Mips 1 and up over box, from mips[0] */
    void volume_shadow_build_mips(const VoxelVolume *vol, const ChunkBox *box, uint8_t *const mips[VOLUME_SHADOW_MIPS]);

    /* Upload regions of the boxes, mip by mip (VOLUME_SHADOW_MIPS * box_count of them, all
     * texels of a box being VOLUME_SHADOW_CHUNK_BYTES per chunk). Returns the staging size. */
    size_t volume_shadow_copy_regions(const ChunkBox *boxes, int32_t box_count, ShadowCopyRegion *out_regions);

    /* Copies the regions' texels from the mips into staging */
    void volume_shadow_stage_regions(const VoxelVolume *vol, const uint8_t *const mips[VOLUME_SHADOW_MIPS],
                                     const ShadowCopyRegion *regions, int32_t region_count, uint8_t *staging);

#ifdef __cplusplus
}
#endif

#endif
