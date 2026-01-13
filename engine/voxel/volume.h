#ifndef PATCH_VOXEL_VOLUME_H
#define PATCH_VOXEL_VOLUME_H

#include "engine/voxel/chunk.h"
#include "engine/core/types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define VOLUME_MAX_CHUNKS_X 16
#define VOLUME_MAX_CHUNKS_Y 8
#define VOLUME_MAX_CHUNKS_Z 16
#define VOLUME_MAX_CHUNKS (VOLUME_MAX_CHUNKS_X * VOLUME_MAX_CHUNKS_Y * VOLUME_MAX_CHUNKS_Z)

#define VOLUME_MAX_DIRTY_PER_FRAME 16
#define VOLUME_MAX_EDITS_PER_TICK 4096
#define VOLUME_MAX_UPLOADS_PER_FRAME 16
#define VOLUME_MAX_FRAGMENTS_PER_TICK 32

#define VOLUME_DIRTY_RING_SIZE 64
#define VOLUME_EDIT_BATCH_MAX_CHUNKS 64
#define VOLUME_CHUNK_BITMAP_SIZE ((VOLUME_MAX_CHUNKS + 63) / 64)
#define VOLUME_SHADOW_DIRTY_MAX 64

    typedef struct
    {
        int32_t chunk_index;
        uint32_t dirty_frame;
    } DirtyChunkEntry;

    typedef struct
    {
        Chunk *chunks;
        int32_t chunks_x, chunks_y, chunks_z;
        int32_t total_chunks;

        Bounds3D bounds;
        float voxel_size;

        DirtyChunkEntry dirty_queue[VOLUME_MAX_DIRTY_PER_FRAME];
        int32_t dirty_count;
        uint32_t current_frame;

        int32_t dirty_ring[VOLUME_DIRTY_RING_SIZE];
        int32_t dirty_ring_head;
        int32_t dirty_ring_tail;
        bool dirty_ring_overflow;

        int32_t edit_touched_chunks[VOLUME_EDIT_BATCH_MAX_CHUNKS];
        int32_t edit_touched_count;
        int32_t edit_count;
        bool edit_batch_active;

        uint64_t edit_touched_bitmap[VOLUME_CHUNK_BITMAP_SIZE];

        int32_t last_edit_chunks[VOLUME_EDIT_BATCH_MAX_CHUNKS];
        int32_t last_edit_count;

        uint64_t dirty_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        int32_t dirty_bitmap_scan_pos;

        int32_t total_solid_voxels;
        int32_t active_chunks;

        uint64_t shadow_dirty_bitmap[VOLUME_CHUNK_BITMAP_SIZE];
        int32_t shadow_dirty_chunks[VOLUME_SHADOW_DIRTY_MAX];
        int32_t shadow_dirty_count;
        bool shadow_needs_full_rebuild;
    } VoxelVolume;

    VoxelVolume *volume_create(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                               Bounds3D bounds);

    VoxelVolume *volume_create_dims(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                    Vec3 origin, float voxel_size);

    void volume_destroy(VoxelVolume *vol);

    void volume_clear(VoxelVolume *vol);

    static inline void volume_world_to_chunk(const VoxelVolume *vol, Vec3 pos,
                                             int32_t *cx, int32_t *cy, int32_t *cz)
    {
        float local_x = pos.x - vol->bounds.min_x;
        float local_y = pos.y - vol->bounds.min_y;
        float local_z = pos.z - vol->bounds.min_z;

        float chunk_world_size = vol->voxel_size * CHUNK_SIZE;

        float fx = local_x / chunk_world_size;
        float fy = local_y / chunk_world_size;
        float fz = local_z / chunk_world_size;

        int32_t ix = (int32_t)fx;
        int32_t iy = (int32_t)fy;
        int32_t iz = (int32_t)fz;
        if ((float)ix > fx)
            ix--;
        if ((float)iy > fy)
            iy--;
        if ((float)iz > fz)
            iz--;

        *cx = ix;
        *cy = iy;
        *cz = iz;
    }

    static inline void volume_world_to_local(const VoxelVolume *vol, Vec3 pos,
                                             int32_t *cx, int32_t *cy, int32_t *cz,
                                             int32_t *lx, int32_t *ly, int32_t *lz)
    {
        float local_x = pos.x - vol->bounds.min_x;
        float local_y = pos.y - vol->bounds.min_y;
        float local_z = pos.z - vol->bounds.min_z;

        float chunk_world_size = vol->voxel_size * CHUNK_SIZE;

        float fx = local_x / chunk_world_size;
        float fy = local_y / chunk_world_size;
        float fz = local_z / chunk_world_size;

        int32_t ix = (int32_t)fx;
        int32_t iy = (int32_t)fy;
        int32_t iz = (int32_t)fz;
        if ((float)ix > fx)
            ix--;
        if ((float)iy > fy)
            iy--;
        if ((float)iz > fz)
            iz--;

        *cx = ix;
        *cy = iy;
        *cz = iz;

        float chunk_base_x = (*cx) * chunk_world_size;
        float chunk_base_y = (*cy) * chunk_world_size;
        float chunk_base_z = (*cz) * chunk_world_size;

        float flx = (local_x - chunk_base_x) / vol->voxel_size;
        float fly = (local_y - chunk_base_y) / vol->voxel_size;
        float flz = (local_z - chunk_base_z) / vol->voxel_size;

        int32_t ilx = (int32_t)flx;
        int32_t ily = (int32_t)fly;
        int32_t ilz = (int32_t)flz;
        if ((float)ilx > flx)
            ilx--;
        if ((float)ily > fly)
            ily--;
        if ((float)ilz > flz)
            ilz--;

        *lx = ilx;
        *ly = ily;
        *lz = ilz;
    }

    static inline Vec3 volume_voxel_to_world(const VoxelVolume *vol,
                                             int32_t cx, int32_t cy, int32_t cz,
                                             int32_t lx, int32_t ly, int32_t lz)
    {
        float chunk_world_size = vol->voxel_size * CHUNK_SIZE;

        Vec3 pos;
        pos.x = vol->bounds.min_x + cx * chunk_world_size + (lx + 0.5f) * vol->voxel_size;
        pos.y = vol->bounds.min_y + cy * chunk_world_size + (ly + 0.5f) * vol->voxel_size;
        pos.z = vol->bounds.min_z + cz * chunk_world_size + (lz + 0.5f) * vol->voxel_size;
        return pos;
    }

    static inline Vec3 volume_world_to_voxel_center(const VoxelVolume *vol, Vec3 pos)
    {
        int32_t cx, cy, cz, lx, ly, lz;
        volume_world_to_local(vol, pos, &cx, &cy, &cz, &lx, &ly, &lz);
        return volume_voxel_to_world(vol, cx, cy, cz, lx, ly, lz);
    }

    static inline Chunk *volume_get_chunk(VoxelVolume *vol, int32_t cx, int32_t cy, int32_t cz)
    {
        if (cx < 0 || cx >= vol->chunks_x ||
            cy < 0 || cy >= vol->chunks_y ||
            cz < 0 || cz >= vol->chunks_z)
        {
            return NULL;
        }
        int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
        return &vol->chunks[idx];
    }

    static inline int32_t volume_chunk_index(const VoxelVolume *vol, int32_t cx, int32_t cy, int32_t cz)
    {
        if (cx < 0 || cx >= vol->chunks_x ||
            cy < 0 || cy >= vol->chunks_y ||
            cz < 0 || cz >= vol->chunks_z)
        {
            return -1;
        }
        return cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    }

    uint8_t volume_get_at(const VoxelVolume *vol, Vec3 pos);
    void volume_set_at(VoxelVolume *vol, Vec3 pos, uint8_t material);
    bool volume_is_solid_at(const VoxelVolume *vol, Vec3 pos);
    int32_t volume_fill_sphere(VoxelVolume *vol, Vec3 center, float radius, uint8_t material);
    int32_t volume_fill_box(VoxelVolume *vol, Vec3 min_corner, Vec3 max_corner, uint8_t material);
    void volume_mark_chunk_dirty(VoxelVolume *vol, int32_t chunk_index);
    void volume_begin_frame(VoxelVolume *vol);
    int32_t volume_get_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_count);
    void volume_mark_chunks_uploaded(VoxelVolume *vol, const int32_t *chunk_indices, int32_t count);
    void volume_rebuild_all_occupancy(VoxelVolume *vol);
    void volume_rebuild_dirty_occupancy(VoxelVolume *vol);
    float volume_raycast(const VoxelVolume *vol, Vec3 origin, Vec3 dir, float max_dist,
                         Vec3 *out_hit_pos, Vec3 *out_hit_normal, uint8_t *out_material);

    void volume_edit_begin(VoxelVolume *vol);
    void volume_edit_set(VoxelVolume *vol, Vec3 pos, uint8_t material);
    int32_t volume_edit_end(VoxelVolume *vol);

    bool volume_ray_hits_any_occupancy(const VoxelVolume *vol, Vec3 origin, Vec3 dir, float max_dist);

    void volume_pack_shadow_volume(const VoxelVolume *vol, uint8_t *out_packed,
                                   uint32_t *out_width, uint32_t *out_height, uint32_t *out_depth);

    void volume_generate_shadow_mips(const uint8_t *mip0, uint32_t w, uint32_t h, uint32_t d,
                                     uint8_t *mip1, uint8_t *mip2);

    int32_t volume_get_shadow_dirty_chunks(VoxelVolume *vol, int32_t *out_indices, int32_t max_count);
    void volume_clear_shadow_dirty(VoxelVolume *vol);
    bool volume_shadow_needs_full_rebuild(const VoxelVolume *vol);

    void volume_pack_shadow_chunk(const VoxelVolume *vol, int32_t chunk_idx,
                                  uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0);

    void volume_generate_shadow_mips_for_chunk(int32_t chunk_idx,
                                               int32_t chunks_x, int32_t chunks_y, int32_t chunks_z,
                                               const uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0,
                                               uint8_t *mip1, uint32_t w1, uint32_t h1, uint32_t d1,
                                               uint8_t *mip2, uint32_t w2, uint32_t h2, uint32_t d2);

#ifdef __cplusplus
}
#endif

#endif
