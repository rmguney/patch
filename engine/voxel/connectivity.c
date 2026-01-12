#include "connectivity.h"
#include "engine/core/math.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool connectivity_work_init(ConnectivityWorkBuffer *work, const VoxelVolume *vol)
{
    if (!work || !vol)
        return false;

    memset(work, 0, sizeof(ConnectivityWorkBuffer));

    int32_t total_voxels = vol->total_chunks * CHUNK_VOXEL_COUNT;

    /* Generation-based visited: 1 byte per voxel instead of 1 bit */
    work->visited_size = total_voxels;
    work->visited_gen = (uint8_t *)calloc(1, (size_t)work->visited_size);
    if (!work->visited_gen)
        return false;

    work->generation = 1;  /* Start at 1; 0 means "never visited" */

    work->island_ids_size = total_voxels;
    work->island_ids = (uint8_t *)calloc(1, (size_t)work->island_ids_size);
    if (!work->island_ids)
    {
        free(work->visited_gen);
        work->visited_gen = NULL;
        return false;
    }

    return true;
}

void connectivity_work_destroy(ConnectivityWorkBuffer *work)
{
    if (!work)
        return;

    if (work->visited_gen)
    {
        free(work->visited_gen);
        work->visited_gen = NULL;
    }
    if (work->island_ids)
    {
        free(work->island_ids);
        work->island_ids = NULL;
    }
}

void connectivity_work_clear(ConnectivityWorkBuffer *work)
{
    if (!work)
        return;

    /* Generation-based clear: increment generation to invalidate all visited stamps */
    work->generation++;
    if (work->generation == 0)
    {
        /* Wrapped around - must do full clear */
        work->generation = 1;
        if (work->visited_gen)
            memset(work->visited_gen, 0, (size_t)work->visited_size);
    }

    if (work->island_ids)
        memset(work->island_ids, 0, (size_t)work->island_ids_size);
    work->stack_top = 0;
}

static inline int32_t global_voxel_index(const VoxelVolume *vol, int32_t cx, int32_t cy, int32_t cz,
                                         int32_t lx, int32_t ly, int32_t lz)
{
    int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
    int32_t local_idx = lx + ly * CHUNK_SIZE + lz * CHUNK_SIZE * CHUNK_SIZE;
    return chunk_idx * CHUNK_VOXEL_COUNT + local_idx;
}

static inline bool is_visited(const ConnectivityWorkBuffer *work, int32_t global_idx)
{
    return work->visited_gen[global_idx] == work->generation;
}

static inline void set_visited(ConnectivityWorkBuffer *work, int32_t global_idx)
{
    work->visited_gen[global_idx] = work->generation;
}

static inline void set_island_id(ConnectivityWorkBuffer *work, int32_t global_idx, uint8_t island_id)
{
    work->island_ids[global_idx] = island_id;
}

static const int32_t NEIGHBOR_OFFSETS[6][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

static inline int32_t pack_voxel_pos(int32_t cx, int32_t cy, int32_t cz,
                                     int32_t lx, int32_t ly, int32_t lz)
{
    /* Bit layout (32 bits total):
     * cx: bits 26-31 (6 bits, 0-63)
     * cy: bits 21-25 (5 bits, 0-31)
     * cz: bits 15-20 (6 bits, 0-63)
     * lx: bits 10-14 (5 bits, 0-31)
     * ly: bits 5-9  (5 bits, 0-31)
     * lz: bits 0-4  (5 bits, 0-31)
     */
    return (cx << 26) | (cy << 21) | (cz << 15) | (lx << 10) | (ly << 5) | lz;
}

static inline void unpack_voxel_pos(int32_t packed,
                                    int32_t *cx, int32_t *cy, int32_t *cz,
                                    int32_t *lx, int32_t *ly, int32_t *lz)
{
    *cx = (packed >> 26) & 0x3F;  /* 6 bits */
    *cy = (packed >> 21) & 0x1F;  /* 5 bits */
    *cz = (packed >> 15) & 0x3F;  /* 6 bits */
    *lx = (packed >> 10) & 0x1F;  /* 5 bits */
    *ly = (packed >> 5) & 0x1F;   /* 5 bits */
    *lz = packed & 0x1F;          /* 5 bits */
}

static void flood_fill_island(const VoxelVolume *vol, ConnectivityWorkBuffer *work,
                              int32_t start_cx, int32_t start_cy, int32_t start_cz,
                              int32_t start_lx, int32_t start_ly, int32_t start_lz,
                              uint8_t island_id, IslandInfo *island, float anchor_y, uint8_t anchor_mat)
{
    work->stack_top = 0;

    int32_t packed = pack_voxel_pos(start_cx, start_cy, start_cz, start_lx, start_ly, start_lz);
    work->stack[work->stack_top++] = packed;

    int32_t global_idx = global_voxel_index(vol, start_cx, start_cy, start_cz,
                                            start_lx, start_ly, start_lz);
    set_visited(work, global_idx);
    set_island_id(work, global_idx, island_id);

    Vec3 com_sum = vec3_zero();
    float mass_sum = 0.0f;

    while (work->stack_top > 0)
    {
        packed = work->stack[--work->stack_top];

        int32_t cx, cy, cz, lx, ly, lz;
        unpack_voxel_pos(packed, &cx, &cy, &cz, &lx, &ly, &lz);

        Chunk *chunk = volume_get_chunk((VoxelVolume *)vol, cx, cy, cz);
        if (!chunk)
            continue;

        uint8_t mat = chunk_get(chunk, lx, ly, lz);
        if (mat == 0)
            continue;

        island->voxel_count++;

        Vec3 world_pos = volume_voxel_to_world(vol, cx, cy, cz, lx, ly, lz);
        com_sum = vec3_add(com_sum, world_pos);
        mass_sum += 1.0f;

        if (world_pos.x < island->min_corner.x)
            island->min_corner.x = world_pos.x;
        if (world_pos.y < island->min_corner.y)
            island->min_corner.y = world_pos.y;
        if (world_pos.z < island->min_corner.z)
            island->min_corner.z = world_pos.z;
        if (world_pos.x > island->max_corner.x)
            island->max_corner.x = world_pos.x;
        if (world_pos.y > island->max_corner.y)
            island->max_corner.y = world_pos.y;
        if (world_pos.z > island->max_corner.z)
            island->max_corner.z = world_pos.z;

        int32_t global_vx = cx * CHUNK_SIZE + lx;
        int32_t global_vy = cy * CHUNK_SIZE + ly;
        int32_t global_vz = cz * CHUNK_SIZE + lz;

        if (global_vx < island->voxel_min_x)
            island->voxel_min_x = global_vx;
        if (global_vy < island->voxel_min_y)
            island->voxel_min_y = global_vy;
        if (global_vz < island->voxel_min_z)
            island->voxel_min_z = global_vz;
        if (global_vx > island->voxel_max_x)
            island->voxel_max_x = global_vx;
        if (global_vy > island->voxel_max_y)
            island->voxel_max_y = global_vy;
        if (global_vz > island->voxel_max_z)
            island->voxel_max_z = global_vz;

        if (world_pos.y <= anchor_y + vol->voxel_size)
        {
            island->anchor = ANCHOR_FLOOR;
        }
        if (anchor_mat != 0 && mat == anchor_mat)
        {
            island->anchor = ANCHOR_MATERIAL;
        }
        if ((cx == 0 || cx == vol->chunks_x - 1 ||
             cz == 0 || cz == vol->chunks_z - 1) &&
            world_pos.y <= anchor_y + vol->voxel_size * 2.0f)
        {
            if (island->anchor == ANCHOR_NONE)
                island->anchor = ANCHOR_VOLUME_EDGE;
        }

        for (int32_t n = 0; n < 6; n++)
        {
            int32_t nx = lx + NEIGHBOR_OFFSETS[n][0];
            int32_t ny = ly + NEIGHBOR_OFFSETS[n][1];
            int32_t nz = lz + NEIGHBOR_OFFSETS[n][2];
            int32_t ncx = cx;
            int32_t ncy = cy;
            int32_t ncz = cz;

            if (nx < 0)
            {
                ncx--;
                nx = CHUNK_SIZE - 1;
            }
            else if (nx >= CHUNK_SIZE)
            {
                ncx++;
                nx = 0;
            }
            if (ny < 0)
            {
                ncy--;
                ny = CHUNK_SIZE - 1;
            }
            else if (ny >= CHUNK_SIZE)
            {
                ncy++;
                ny = 0;
            }
            if (nz < 0)
            {
                ncz--;
                nz = CHUNK_SIZE - 1;
            }
            else if (nz >= CHUNK_SIZE)
            {
                ncz++;
                nz = 0;
            }

            if (ncx < 0 || ncx >= vol->chunks_x ||
                ncy < 0 || ncy >= vol->chunks_y ||
                ncz < 0 || ncz >= vol->chunks_z)
            {
                continue;
            }

            int32_t neighbor_global = global_voxel_index(vol, ncx, ncy, ncz, nx, ny, nz);
            if (is_visited(work, neighbor_global))
                continue;

            Chunk *neighbor_chunk = volume_get_chunk((VoxelVolume *)vol, ncx, ncy, ncz);
            if (!neighbor_chunk)
                continue;

            if (chunk_get(neighbor_chunk, nx, ny, nz) == 0)
                continue;

            set_visited(work, neighbor_global);
            set_island_id(work, neighbor_global, island_id);

            if (work->stack_top < CONNECTIVITY_WORK_STACK_SIZE)
            {
                int32_t neighbor_packed = pack_voxel_pos(ncx, ncy, ncz, nx, ny, nz);
                work->stack[work->stack_top++] = neighbor_packed;
            }
            else
            {
                /* Stack overflow: force anchor to be safe, but note that this
                   neighbor's neighbors won't be explored, potentially orphaning them. */
                island->anchor = ANCHOR_FLOOR;
            }
        }
    }

    if (mass_sum > 0.0f)
    {
        island->center_of_mass = vec3_scale(com_sum, 1.0f / mass_sum);
        island->total_mass = mass_sum;
    }
    island->is_floating = (island->anchor == ANCHOR_NONE);
}

void connectivity_analyze_region(const VoxelVolume *vol,
                                 Vec3 region_min, Vec3 region_max,
                                 float anchor_y, uint8_t anchor_material,
                                 ConnectivityWorkBuffer *work,
                                 ConnectivityResult *result)
{
    if (!vol || !work || !result)
        return;

    memset(result, 0, sizeof(ConnectivityResult));
    connectivity_work_clear(work);

    int32_t start_cx, start_cy, start_cz;
    int32_t end_cx, end_cy, end_cz;
    volume_world_to_chunk(vol, region_min, &start_cx, &start_cy, &start_cz);
    volume_world_to_chunk(vol, region_max, &end_cx, &end_cy, &end_cz);

    if (start_cx < 0)
        start_cx = 0;
    if (start_cy < 0)
        start_cy = 0;
    if (start_cz < 0)
        start_cz = 0;
    if (end_cx >= vol->chunks_x)
        end_cx = vol->chunks_x - 1;
    if (end_cy >= vol->chunks_y)
        end_cy = vol->chunks_y - 1;
    if (end_cz >= vol->chunks_z)
        end_cz = vol->chunks_z - 1;

    uint8_t next_island_id = 1;

    for (int32_t cz = start_cz; cz <= end_cz; cz++)
    {
        for (int32_t cy = start_cy; cy <= end_cy; cy++)
        {
            for (int32_t cx = start_cx; cx <= end_cx; cx++)
            {
                Chunk *chunk = volume_get_chunk((VoxelVolume *)vol, cx, cy, cz);
                if (!chunk || !chunk->occupancy.has_any)
                    continue;

                for (int32_t lz = 0; lz < CHUNK_SIZE; lz++)
                {
                    for (int32_t ly = 0; ly < CHUNK_SIZE; ly++)
                    {
                        for (int32_t lx = 0; lx < CHUNK_SIZE; lx++)
                        {
                            int32_t global_idx = global_voxel_index(vol, cx, cy, cz, lx, ly, lz);

                            if (is_visited(work, global_idx))
                                continue;

                            uint8_t mat = chunk_get(chunk, lx, ly, lz);
                            if (mat == 0)
                            {
                                set_visited(work, global_idx);
                                continue;
                            }

                            result->total_voxels_checked++;

                            if (result->island_count >= CONNECTIVITY_MAX_ISLANDS)
                                break;

                            IslandInfo *island = &result->islands[result->island_count];
                            memset(island, 0, sizeof(IslandInfo));
                            island->island_id = next_island_id;
                            island->min_corner = vec3_create(1e30f, 1e30f, 1e30f);
                            island->max_corner = vec3_create(-1e30f, -1e30f, -1e30f);
                            island->voxel_min_x = INT32_MAX;
                            island->voxel_min_y = INT32_MAX;
                            island->voxel_min_z = INT32_MAX;
                            island->voxel_max_x = INT32_MIN;
                            island->voxel_max_y = INT32_MIN;
                            island->voxel_max_z = INT32_MIN;

                            flood_fill_island(vol, work, cx, cy, cz, lx, ly, lz,
                                              next_island_id, island, anchor_y, anchor_material);

                            if (island->is_floating)
                                result->floating_count++;
                            else
                                result->anchored_count++;

                            result->island_count++;
                            next_island_id++;
                        }
                    }
                }
            }
        }
    }
}

void connectivity_analyze_volume(const VoxelVolume *vol,
                                 float anchor_y, uint8_t anchor_material,
                                 ConnectivityWorkBuffer *work,
                                 ConnectivityResult *result)
{
    PROFILE_BEGIN(PROFILE_SIM_CONNECTIVITY);

    if (!vol)
    {
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    Vec3 region_min = vec3_create(vol->bounds.min_x, vol->bounds.min_y, vol->bounds.min_z);
    Vec3 region_max = vec3_create(vol->bounds.max_x, vol->bounds.max_y, vol->bounds.max_z);

    connectivity_analyze_region(vol, region_min, region_max, anchor_y, anchor_material, work, result);

    PROFILE_END(PROFILE_SIM_CONNECTIVITY);
}

void connectivity_analyze_dirty(const VoxelVolume *vol,
                                float anchor_y, uint8_t anchor_material,
                                ConnectivityWorkBuffer *work,
                                ConnectivityResult *result)
{
    PROFILE_BEGIN(PROFILE_SIM_CONNECTIVITY);

    if (!vol || !work || !result)
    {
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    if (vol->last_edit_count == 0)
    {
        memset(result, 0, sizeof(ConnectivityResult));
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    int32_t min_cx = vol->chunks_x, min_cy = vol->chunks_y, min_cz = vol->chunks_z;
    int32_t max_cx = -1, max_cy = -1, max_cz = -1;

    for (int32_t i = 0; i < vol->last_edit_count; i++)
    {
        int32_t chunk_idx = vol->last_edit_chunks[i];
        if (chunk_idx < 0 || chunk_idx >= vol->total_chunks)
            continue;

        int32_t cx = chunk_idx % vol->chunks_x;
        int32_t cy = (chunk_idx / vol->chunks_x) % vol->chunks_y;
        int32_t cz = chunk_idx / (vol->chunks_x * vol->chunks_y);

        if (cx < min_cx)
            min_cx = cx;
        if (cy < min_cy)
            min_cy = cy;
        if (cz < min_cz)
            min_cz = cz;
        if (cx > max_cx)
            max_cx = cx;
        if (cy > max_cy)
            max_cy = cy;
        if (cz > max_cz)
            max_cz = cz;
    }

    if (max_cx < 0)
    {
        memset(result, 0, sizeof(ConnectivityResult));
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    min_cx = (min_cx > 0) ? min_cx - 1 : 0;
    min_cy = (min_cy > 0) ? min_cy - 1 : 0;
    min_cz = (min_cz > 0) ? min_cz - 1 : 0;
    max_cx = (max_cx < vol->chunks_x - 1) ? max_cx + 1 : vol->chunks_x - 1;
    max_cy = (max_cy < vol->chunks_y - 1) ? max_cy + 1 : vol->chunks_y - 1;
    max_cz = (max_cz < vol->chunks_z - 1) ? max_cz + 1 : vol->chunks_z - 1;

    float chunk_world_size = vol->voxel_size * CHUNK_SIZE;
    Vec3 region_min = vec3_create(
        vol->bounds.min_x + min_cx * chunk_world_size,
        vol->bounds.min_y + min_cy * chunk_world_size,
        vol->bounds.min_z + min_cz * chunk_world_size);
    Vec3 region_max = vec3_create(
        vol->bounds.min_x + (max_cx + 1) * chunk_world_size,
        vol->bounds.min_y + (max_cy + 1) * chunk_world_size,
        vol->bounds.min_z + (max_cz + 1) * chunk_world_size);

    connectivity_analyze_region(vol, region_min, region_max, anchor_y, anchor_material, work, result);

    PROFILE_END(PROFILE_SIM_CONNECTIVITY);
}

int32_t connectivity_extract_island_with_ids(const VoxelVolume *vol,
                                             const IslandInfo *island,
                                             const ConnectivityWorkBuffer *work,
                                             uint8_t *out_voxels,
                                             int32_t out_size_x, int32_t out_size_y, int32_t out_size_z,
                                             Vec3 *out_origin)
{
    if (!vol || !island || !work || !work->island_ids || !out_voxels)
        return 0;

    int32_t size_x = island->voxel_max_x - island->voxel_min_x + 1;
    int32_t size_y = island->voxel_max_y - island->voxel_min_y + 1;
    int32_t size_z = island->voxel_max_z - island->voxel_min_z + 1;

    if (size_x > out_size_x || size_y > out_size_y || size_z > out_size_z)
        return 0;

    memset(out_voxels, 0, (size_t)(out_size_x * out_size_y * out_size_z));

    if (out_origin)
    {
        out_origin->x = vol->bounds.min_x + island->voxel_min_x * vol->voxel_size;
        out_origin->y = vol->bounds.min_y + island->voxel_min_y * vol->voxel_size;
        out_origin->z = vol->bounds.min_z + island->voxel_min_z * vol->voxel_size;
    }

    uint8_t target_id = (uint8_t)island->island_id;
    int32_t copied = 0;

    for (int32_t gz = island->voxel_min_z; gz <= island->voxel_max_z; gz++)
    {
        int32_t cz = gz / CHUNK_SIZE;
        int32_t lz = gz % CHUNK_SIZE;

        for (int32_t gy = island->voxel_min_y; gy <= island->voxel_max_y; gy++)
        {
            int32_t cy = gy / CHUNK_SIZE;
            int32_t ly = gy % CHUNK_SIZE;

            for (int32_t gx = island->voxel_min_x; gx <= island->voxel_max_x; gx++)
            {
                int32_t cx = gx / CHUNK_SIZE;
                int32_t lx = gx % CHUNK_SIZE;

                int32_t global_idx = global_voxel_index(vol, cx, cy, cz, lx, ly, lz);
                if (global_idx < 0 || global_idx >= work->island_ids_size)
                    continue;

                if (work->island_ids[global_idx] != target_id)
                    continue;

                Chunk *chunk = volume_get_chunk((VoxelVolume *)vol, cx, cy, cz);
                if (!chunk)
                    continue;

                uint8_t mat = chunk_get(chunk, lx, ly, lz);
                if (mat == 0)
                    continue;

                int32_t ox = gx - island->voxel_min_x;
                int32_t oy = gy - island->voxel_min_y;
                int32_t oz = gz - island->voxel_min_z;
                int32_t out_idx = ox + oy * out_size_x + oz * out_size_x * out_size_y;

                out_voxels[out_idx] = mat;
                copied++;
            }
        }
    }

    return copied;
}

void connectivity_remove_island(VoxelVolume *vol, const IslandInfo *island,
                                const ConnectivityWorkBuffer *work)
{
    if (!vol || !island || !work || !work->island_ids)
        return;

    uint8_t target_id = (uint8_t)island->island_id;
    if (target_id == 0)
        return;

    volume_edit_begin(vol);

    for (int32_t gz = island->voxel_min_z; gz <= island->voxel_max_z; gz++)
    {
        int32_t cz = gz / CHUNK_SIZE;
        int32_t lz = gz % CHUNK_SIZE;

        for (int32_t gy = island->voxel_min_y; gy <= island->voxel_max_y; gy++)
        {
            int32_t cy = gy / CHUNK_SIZE;
            int32_t ly = gy % CHUNK_SIZE;

            for (int32_t gx = island->voxel_min_x; gx <= island->voxel_max_x; gx++)
            {
                int32_t cx = gx / CHUNK_SIZE;
                int32_t lx = gx % CHUNK_SIZE;

                int32_t global_idx = global_voxel_index(vol, cx, cy, cz, lx, ly, lz);
                if (global_idx < 0 || global_idx >= work->island_ids_size)
                    continue;

                if (work->island_ids[global_idx] != target_id)
                    continue;

                Vec3 world_pos = vec3_create(
                    vol->bounds.min_x + (gx + 0.5f) * vol->voxel_size,
                    vol->bounds.min_y + (gy + 0.5f) * vol->voxel_size,
                    vol->bounds.min_z + (gz + 0.5f) * vol->voxel_size);

                volume_edit_set(vol, world_pos, MATERIAL_EMPTY);
            }
        }
    }

    volume_edit_end(vol);
}
