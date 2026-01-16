#include "unified_volume.h"
#include "engine/voxel/chunk.h"
#include <stdlib.h>
#include <string.h>

static Vec3 quat_rotate_vec3(Quat q, Vec3 v)
{
    float qx = q.x, qy = q.y, qz = q.z, qw = q.w;
    float vx = v.x, vy = v.y, vz = v.z;

    float tx = 2.0f * (qy * vz - qz * vy);
    float ty = 2.0f * (qz * vx - qx * vz);
    float tz = 2.0f * (qx * vy - qy * vx);

    Vec3 result;
    result.x = vx + qw * tx + (qy * tz - qz * ty);
    result.y = vy + qw * ty + (qz * tx - qx * tz);
    result.z = vz + qw * tz + (qx * ty - qy * tx);
    return result;
}

UnifiedVolume *unified_volume_create(int32_t size_x, int32_t size_y, int32_t size_z,
                                     Vec3 origin, float voxel_size)
{
    UnifiedVolume *vol = (UnifiedVolume *)calloc(1, sizeof(UnifiedVolume));
    if (!vol)
        return NULL;

    vol->size_x = size_x;
    vol->size_y = size_y;
    vol->size_z = size_z;
    vol->voxel_size = voxel_size;
    vol->origin = origin;

    vol->chunks_x = (size_x + UNIFIED_CHUNK_SIZE - 1) / UNIFIED_CHUNK_SIZE;
    vol->chunks_y = (size_y + UNIFIED_CHUNK_SIZE - 1) / UNIFIED_CHUNK_SIZE;
    vol->chunks_z = (size_z + UNIFIED_CHUNK_SIZE - 1) / UNIFIED_CHUNK_SIZE;
    vol->total_chunks = vol->chunks_x * vol->chunks_y * vol->chunks_z;

    vol->bounds.min_x = origin.x;
    vol->bounds.min_y = origin.y;
    vol->bounds.min_z = origin.z;
    vol->bounds.max_x = origin.x + size_x * voxel_size;
    vol->bounds.max_y = origin.y + size_y * voxel_size;
    vol->bounds.max_z = origin.z + size_z * voxel_size;

    size_t material_count = (size_t)size_x * size_y * size_z;
    vol->materials = (uint8_t *)calloc(material_count, sizeof(uint8_t));
    if (!vol->materials)
    {
        free(vol);
        return NULL;
    }

    vol->region_masks = (uint64_t *)calloc(vol->total_chunks, sizeof(uint64_t));
    if (!vol->region_masks)
    {
        free(vol->materials);
        free(vol);
        return NULL;
    }

    vol->chunk_occupancy = (uint8_t *)calloc(vol->total_chunks, sizeof(uint8_t));
    if (!vol->chunk_occupancy)
    {
        free(vol->region_masks);
        free(vol->materials);
        free(vol);
        return NULL;
    }

    vol->needs_full_rebuild = true;
    vol->terrain_stamped = false;

    return vol;
}

void unified_volume_destroy(UnifiedVolume *vol)
{
    if (!vol)
        return;
    free(vol->chunk_occupancy);
    free(vol->region_masks);
    free(vol->materials);
    free(vol);
}

void unified_volume_clear(UnifiedVolume *vol)
{
    if (!vol)
        return;

    size_t material_count = (size_t)vol->size_x * vol->size_y * vol->size_z;
    memset(vol->materials, 0, material_count);
    memset(vol->region_masks, 0, vol->total_chunks * sizeof(uint64_t));
    memset(vol->chunk_occupancy, 0, vol->total_chunks * sizeof(uint8_t));

    vol->terrain_stamped = false;
    vol->needs_full_rebuild = true;
}

static void mark_voxel_occupied(UnifiedVolume *vol, int32_t x, int32_t y, int32_t z)
{
    int32_t cx = x / UNIFIED_CHUNK_SIZE;
    int32_t cy = y / UNIFIED_CHUNK_SIZE;
    int32_t cz = z / UNIFIED_CHUNK_SIZE;

    if (cx < 0 || cx >= vol->chunks_x ||
        cy < 0 || cy >= vol->chunks_y ||
        cz < 0 || cz >= vol->chunks_z)
        return;

    int32_t chunk_idx = unified_volume_chunk_index(vol, cx, cy, cz);
    vol->chunk_occupancy[chunk_idx] = 1;

    int32_t lx = x - cx * UNIFIED_CHUNK_SIZE;
    int32_t ly = y - cy * UNIFIED_CHUNK_SIZE;
    int32_t lz = z - cz * UNIFIED_CHUNK_SIZE;

    int32_t rx = lx / UNIFIED_REGION_SIZE;
    int32_t ry = ly / UNIFIED_REGION_SIZE;
    int32_t rz = lz / UNIFIED_REGION_SIZE;
    int32_t region_idx = rx + ry * UNIFIED_REGIONS_PER_CHUNK +
                         rz * UNIFIED_REGIONS_PER_CHUNK * UNIFIED_REGIONS_PER_CHUNK;

    vol->region_masks[chunk_idx] |= (1ULL << region_idx);
}

void unified_volume_stamp_terrain(UnifiedVolume *vol, const VoxelVolume *terrain)
{
    if (!vol || !terrain)
        return;

    for (int32_t ci = 0; ci < terrain->total_chunks; ci++)
    {
        const Chunk *chunk = &terrain->chunks[ci];
        if (!chunk->occupancy.has_any)
            continue;

        int32_t chunk_base_x = chunk->coord_x * CHUNK_SIZE;
        int32_t chunk_base_y = chunk->coord_y * CHUNK_SIZE;
        int32_t chunk_base_z = chunk->coord_z * CHUNK_SIZE;

        for (int32_t z = 0; z < CHUNK_SIZE; z++)
        {
            for (int32_t y = 0; y < CHUNK_SIZE; y++)
            {
                for (int32_t x = 0; x < CHUNK_SIZE; x++)
                {
                    int32_t local_idx = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
                    uint8_t mat = chunk->voxels[local_idx].material;
                    if (mat == 0)
                        continue;

                    int32_t world_x = chunk_base_x + x;
                    int32_t world_y = chunk_base_y + y;
                    int32_t world_z = chunk_base_z + z;

                    if (world_x < 0 || world_x >= vol->size_x ||
                        world_y < 0 || world_y >= vol->size_y ||
                        world_z < 0 || world_z >= vol->size_z)
                        continue;

                    int32_t vol_idx = unified_volume_voxel_index(vol, world_x, world_y, world_z);
                    vol->materials[vol_idx] = mat;
                    mark_voxel_occupied(vol, world_x, world_y, world_z);
                }
            }
        }
    }

    vol->terrain_stamped = true;
}

void unified_volume_stamp_object(UnifiedVolume *vol, const VoxelObject *obj)
{
    if (!vol || !obj || !obj->active)
        return;

    float half_grid = (VOBJ_GRID_SIZE * obj->voxel_size) * 0.5f;

    for (int32_t oz = 0; oz < VOBJ_GRID_SIZE; oz++)
    {
        for (int32_t oy = 0; oy < VOBJ_GRID_SIZE; oy++)
        {
            for (int32_t ox = 0; ox < VOBJ_GRID_SIZE; ox++)
            {
                int32_t local_idx = vobj_index(ox, oy, oz);
                uint8_t mat = obj->voxels[local_idx].material;
                if (mat == 0)
                    continue;

                float local_x = (ox + 0.5f) * obj->voxel_size - half_grid;
                float local_y = (oy + 0.5f) * obj->voxel_size - half_grid;
                float local_z = (oz + 0.5f) * obj->voxel_size - half_grid;

                Vec3 local_pos = {local_x, local_y, local_z};
                Vec3 world_pos = quat_rotate_vec3(obj->orientation, local_pos);
                world_pos.x += obj->position.x;
                world_pos.y += obj->position.y;
                world_pos.z += obj->position.z;

                int32_t vx, vy, vz;
                unified_volume_world_to_voxel(vol, world_pos, &vx, &vy, &vz);

                if (vx < 0 || vx >= vol->size_x ||
                    vy < 0 || vy >= vol->size_y ||
                    vz < 0 || vz >= vol->size_z)
                    continue;

                int32_t vol_idx = unified_volume_voxel_index(vol, vx, vy, vz);
                vol->materials[vol_idx] = mat;
                mark_voxel_occupied(vol, vx, vy, vz);
            }
        }
    }
}

void unified_volume_stamp_particle(UnifiedVolume *vol, Vec3 pos, float radius, uint8_t material)
{
    if (!vol || material == 0)
        return;

    int32_t min_x, min_y, min_z, max_x, max_y, max_z;
    Vec3 min_pos = {pos.x - radius, pos.y - radius, pos.z - radius};
    Vec3 max_pos = {pos.x + radius, pos.y + radius, pos.z + radius};

    unified_volume_world_to_voxel(vol, min_pos, &min_x, &min_y, &min_z);
    unified_volume_world_to_voxel(vol, max_pos, &max_x, &max_y, &max_z);

    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (min_z < 0)
        min_z = 0;
    if (max_x >= vol->size_x)
        max_x = vol->size_x - 1;
    if (max_y >= vol->size_y)
        max_y = vol->size_y - 1;
    if (max_z >= vol->size_z)
        max_z = vol->size_z - 1;

    for (int32_t z = min_z; z <= max_z; z++)
    {
        for (int32_t y = min_y; y <= max_y; y++)
        {
            for (int32_t x = min_x; x <= max_x; x++)
            {
                int32_t vol_idx = unified_volume_voxel_index(vol, x, y, z);
                vol->materials[vol_idx] = material;
                mark_voxel_occupied(vol, x, y, z);
            }
        }
    }
}

void unified_volume_stamp_objects(UnifiedVolume *vol, const VoxelObjectWorld *world)
{
    if (!vol || !world)
        return;

    for (int32_t i = 0; i < world->object_count; i++)
    {
        const VoxelObject *obj = &world->objects[i];
        if (obj->active)
        {
            unified_volume_stamp_object(vol, obj);
        }
    }
}

void unified_volume_stamp_particles(UnifiedVolume *vol, const ParticleSystem *particles)
{
    if (!vol || !particles)
        return;

    for (int32_t i = 0; i < particles->count; i++)
    {
        const Particle *p = &particles->particles[i];
        if (!p->active)
            continue;

        /* Particles use a default material (254) since they store color, not material ID */
        unified_volume_stamp_particle(vol, p->position, p->radius, 254);
    }
}

void unified_volume_update_hierarchy(UnifiedVolume *vol)
{
    (void)vol;
}

void unified_volume_mark_dirty(UnifiedVolume *vol, int32_t chunk_idx)
{
    if (!vol || chunk_idx < 0 || chunk_idx >= vol->total_chunks)
        return;

    int32_t bitmap_idx = chunk_idx / 64;
    int32_t bit_idx = chunk_idx % 64;

    if (vol->dirty_bitmap[bitmap_idx] & (1ULL << bit_idx))
        return;

    vol->dirty_bitmap[bitmap_idx] |= (1ULL << bit_idx);

    if (vol->dirty_count < UNIFIED_MAX_DIRTY_CHUNKS)
    {
        vol->dirty_chunks[vol->dirty_count++] = chunk_idx;
    }
    else
    {
        vol->needs_full_rebuild = true;
    }
}

int32_t unified_volume_get_dirty_chunks(const UnifiedVolume *vol, int32_t *out_indices, int32_t max_count)
{
    if (!vol || !out_indices || max_count <= 0)
        return 0;

    int32_t count = vol->dirty_count < max_count ? vol->dirty_count : max_count;
    for (int32_t i = 0; i < count; i++)
    {
        out_indices[i] = vol->dirty_chunks[i];
    }
    return count;
}

void unified_volume_clear_dirty(UnifiedVolume *vol)
{
    if (!vol)
        return;

    memset(vol->dirty_bitmap, 0, sizeof(vol->dirty_bitmap));
    vol->dirty_count = 0;
    vol->needs_full_rebuild = false;
}

static void stamp_voxel_to_shadow(uint8_t *shadow_mip0, uint32_t w, uint32_t h, uint32_t d,
                                  int32_t vx, int32_t vy, int32_t vz)
{
    int32_t px = vx >> 1;
    int32_t py = vy >> 1;
    int32_t pz = vz >> 1;

    if (px < 0 || px >= (int32_t)w ||
        py < 0 || py >= (int32_t)h ||
        pz < 0 || pz >= (int32_t)d)
        return;

    int32_t bit_idx = (vx & 1) + ((vy & 1) << 1) + ((vz & 1) << 2);
    size_t packed_idx = (size_t)px + (size_t)py * w + (size_t)pz * w * h;
    shadow_mip0[packed_idx] |= (uint8_t)(1 << bit_idx);
}

void unified_volume_stamp_objects_to_shadow(uint8_t *shadow_mip0, uint32_t w, uint32_t h, uint32_t d,
                                            const VoxelVolume *terrain, const VoxelObjectWorld *objects)
{
    if (!shadow_mip0 || !terrain || !objects)
        return;

    for (int32_t i = 0; i < objects->object_count; i++)
    {
        const VoxelObject *obj = &objects->objects[i];
        if (!obj->active)
            continue;

        float half_grid = (VOBJ_GRID_SIZE * obj->voxel_size) * 0.5f;

        for (int32_t oz = 0; oz < VOBJ_GRID_SIZE; oz++)
        {
            for (int32_t oy = 0; oy < VOBJ_GRID_SIZE; oy++)
            {
                for (int32_t ox = 0; ox < VOBJ_GRID_SIZE; ox++)
                {
                    int32_t local_idx = vobj_index(ox, oy, oz);
                    uint8_t mat = obj->voxels[local_idx].material;
                    if (mat == 0)
                        continue;

                    float local_x = (ox + 0.5f) * obj->voxel_size - half_grid;
                    float local_y = (oy + 0.5f) * obj->voxel_size - half_grid;
                    float local_z = (oz + 0.5f) * obj->voxel_size - half_grid;

                    Vec3 local_pos = {local_x, local_y, local_z};
                    Vec3 world_pos = quat_rotate_vec3(obj->orientation, local_pos);
                    world_pos.x += obj->position.x;
                    world_pos.y += obj->position.y;
                    world_pos.z += obj->position.z;

                    float rel_x = world_pos.x - terrain->bounds.min_x;
                    float rel_y = world_pos.y - terrain->bounds.min_y;
                    float rel_z = world_pos.z - terrain->bounds.min_z;

                    int32_t vx = (int32_t)(rel_x / terrain->voxel_size);
                    int32_t vy = (int32_t)(rel_y / terrain->voxel_size);
                    int32_t vz = (int32_t)(rel_z / terrain->voxel_size);

                    stamp_voxel_to_shadow(shadow_mip0, w, h, d, vx, vy, vz);
                }
            }
        }
    }
}

void unified_volume_stamp_particles_to_shadow(uint8_t *shadow_mip0, uint32_t w, uint32_t h, uint32_t d,
                                              const VoxelVolume *terrain, const ParticleSystem *particles,
                                              float interp_alpha)
{
    if (!shadow_mip0 || !terrain || !particles)
        return;

    for (int32_t i = 0; i < particles->count; i++)
    {
        const Particle *p = &particles->particles[i];
        if (!p->active)
            continue;

        /* Interpolate between previous and current position */
        float interp_x = p->prev_position.x + interp_alpha * (p->position.x - p->prev_position.x);
        float interp_y = p->prev_position.y + interp_alpha * (p->position.y - p->prev_position.y);
        float interp_z = p->prev_position.z + interp_alpha * (p->position.z - p->prev_position.z);

        float rel_x = interp_x - terrain->bounds.min_x;
        float rel_y = interp_y - terrain->bounds.min_y;
        float rel_z = interp_z - terrain->bounds.min_z;

        float radius_voxels = p->radius / terrain->voxel_size;
        int32_t min_vx = (int32_t)((rel_x - p->radius) / terrain->voxel_size);
        int32_t min_vy = (int32_t)((rel_y - p->radius) / terrain->voxel_size);
        int32_t min_vz = (int32_t)((rel_z - p->radius) / terrain->voxel_size);
        int32_t max_vx = (int32_t)((rel_x + p->radius) / terrain->voxel_size);
        int32_t max_vy = (int32_t)((rel_y + p->radius) / terrain->voxel_size);
        int32_t max_vz = (int32_t)((rel_z + p->radius) / terrain->voxel_size);

        (void)radius_voxels;

        for (int32_t vz = min_vz; vz <= max_vz; vz++)
        {
            for (int32_t vy = min_vy; vy <= max_vy; vy++)
            {
                for (int32_t vx = min_vx; vx <= max_vx; vx++)
                {
                    stamp_voxel_to_shadow(shadow_mip0, w, h, d, vx, vy, vz);
                }
            }
        }
    }
}
