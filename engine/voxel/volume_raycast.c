#include "volume.h"
#include "engine/core/math.h"
#include "engine/core/profile.h"
#include <math.h>

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
