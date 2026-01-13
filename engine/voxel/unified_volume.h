#ifndef PATCH_UNIFIED_VOLUME_H
#define PATCH_UNIFIED_VOLUME_H

#include "engine/core/types.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UNIFIED_CHUNK_SIZE 32
#define UNIFIED_REGION_SIZE 8
#define UNIFIED_REGIONS_PER_CHUNK (UNIFIED_CHUNK_SIZE / UNIFIED_REGION_SIZE)
#define UNIFIED_REGION_COUNT (UNIFIED_REGIONS_PER_CHUNK * UNIFIED_REGIONS_PER_CHUNK * UNIFIED_REGIONS_PER_CHUNK)

#define UNIFIED_MAX_CHUNKS_X 16
#define UNIFIED_MAX_CHUNKS_Y 8
#define UNIFIED_MAX_CHUNKS_Z 16
#define UNIFIED_MAX_CHUNKS (UNIFIED_MAX_CHUNKS_X * UNIFIED_MAX_CHUNKS_Y * UNIFIED_MAX_CHUNKS_Z)
#define UNIFIED_CHUNK_BITMAP_SIZE ((UNIFIED_MAX_CHUNKS + 63) / 64)

#define UNIFIED_MAX_DIRTY_CHUNKS 64

    typedef struct
    {
        uint8_t *materials;
        int32_t size_x, size_y, size_z;
        int32_t chunks_x, chunks_y, chunks_z;
        int32_t total_chunks;
        float voxel_size;
        Vec3 origin;
        Bounds3D bounds;

        uint64_t *region_masks;
        uint8_t *chunk_occupancy;

        uint64_t dirty_bitmap[UNIFIED_CHUNK_BITMAP_SIZE];
        int32_t dirty_chunks[UNIFIED_MAX_DIRTY_CHUNKS];
        int32_t dirty_count;
        bool needs_full_rebuild;

        bool terrain_stamped;
    } UnifiedVolume;

    UnifiedVolume *unified_volume_create(int32_t size_x, int32_t size_y, int32_t size_z,
                                         Vec3 origin, float voxel_size);

    void unified_volume_destroy(UnifiedVolume *vol);

    void unified_volume_clear(UnifiedVolume *vol);

    void unified_volume_stamp_terrain(UnifiedVolume *vol, const VoxelVolume *terrain);

    void unified_volume_stamp_object(UnifiedVolume *vol, const VoxelObject *obj);

    void unified_volume_stamp_particle(UnifiedVolume *vol, Vec3 pos, float radius, uint8_t material);

    void unified_volume_stamp_objects(UnifiedVolume *vol, const VoxelObjectWorld *world);

    void unified_volume_stamp_particles(UnifiedVolume *vol, const ParticleSystem *particles);

    void unified_volume_update_hierarchy(UnifiedVolume *vol);

    void unified_volume_mark_dirty(UnifiedVolume *vol, int32_t chunk_idx);

    int32_t unified_volume_get_dirty_chunks(const UnifiedVolume *vol, int32_t *out_indices, int32_t max_count);

    void unified_volume_clear_dirty(UnifiedVolume *vol);

    void unified_volume_stamp_objects_to_shadow(uint8_t *shadow_mip0, uint32_t w, uint32_t h, uint32_t d,
                                                const VoxelVolume *terrain, const VoxelObjectWorld *objects);

    void unified_volume_stamp_particles_to_shadow(uint8_t *shadow_mip0, uint32_t w, uint32_t h, uint32_t d,
                                                  const VoxelVolume *terrain, const ParticleSystem *particles);

    static inline int32_t unified_volume_chunk_index(const UnifiedVolume *vol,
                                                     int32_t cx, int32_t cy, int32_t cz)
    {
        return cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    }

    static inline int32_t unified_volume_voxel_index(const UnifiedVolume *vol,
                                                     int32_t x, int32_t y, int32_t z)
    {
        return x + y * vol->size_x + z * vol->size_x * vol->size_y;
    }

    static inline void unified_volume_world_to_voxel(const UnifiedVolume *vol, Vec3 pos,
                                                     int32_t *vx, int32_t *vy, int32_t *vz)
    {
        float local_x = pos.x - vol->origin.x;
        float local_y = pos.y - vol->origin.y;
        float local_z = pos.z - vol->origin.z;

        *vx = (int32_t)(local_x / vol->voxel_size);
        *vy = (int32_t)(local_y / vol->voxel_size);
        *vz = (int32_t)(local_z / vol->voxel_size);
    }

#ifdef __cplusplus
}
#endif

#endif /* PATCH_UNIFIED_VOLUME_H */
