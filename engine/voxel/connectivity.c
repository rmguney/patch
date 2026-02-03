#include "connectivity.h"
#include "engine/core/math.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>

bool connectivity_work_init(ConnectivityWorkBuffer *work, const VoxelVolume *vol)
{
    if (!work || !vol)
        return false;

    memset(work, 0, sizeof(ConnectivityWorkBuffer));

    int32_t total_voxels = vol->total_chunks * CHUNK_VOXEL_COUNT;

    /* BFS stack sized to volume (capped to avoid excessive allocation) */
    int32_t stack_size = total_voxels;
    if (stack_size > CONNECTIVITY_MAX_STACK_SIZE)
        stack_size = CONNECTIVITY_MAX_STACK_SIZE;
    if (stack_size < CONNECTIVITY_MIN_STACK_SIZE)
        stack_size = CONNECTIVITY_MIN_STACK_SIZE;

    work->stack = (int32_t *)malloc((size_t)stack_size * sizeof(int32_t));
    if (!work->stack)
        return false;
    work->stack_capacity = stack_size;

    /* Generation-based visited: 1 byte per voxel instead of 1 bit */
    work->visited_size = total_voxels;
    work->visited_gen = (uint16_t *)calloc((size_t)work->visited_size, sizeof(uint16_t));
    if (!work->visited_gen)
    {
        free(work->stack);
        work->stack = NULL;
        return false;
    }

    work->generation = 1;  /* Start at 1; 0 means "never visited" */

    work->island_ids_size = total_voxels;
    work->island_ids = (uint16_t *)calloc((size_t)work->island_ids_size, sizeof(uint16_t));
    if (!work->island_ids)
    {
        free(work->stack);
        work->stack = NULL;
        free(work->visited_gen);
        work->visited_gen = NULL;
        return false;
    }

    work->deferred_seed_start = 0;
    work->has_deferred_work = false;

    return true;
}

void connectivity_work_destroy(ConnectivityWorkBuffer *work)
{
    if (!work)
        return;

    if (work->stack)
    {
        free(work->stack);
        work->stack = NULL;
    }
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
            memset(work->visited_gen, 0, (size_t)work->visited_size * sizeof(uint16_t));
    }

    if (work->island_ids)
        memset(work->island_ids, 0, (size_t)work->island_ids_size * sizeof(uint16_t));
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

static inline void set_island_id(ConnectivityWorkBuffer *work, int32_t global_idx, uint16_t island_id)
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

typedef struct {
    int32_t min_cx, min_cy, min_cz;
    int32_t max_cx, max_cy, max_cz;
    bool bounded;
} FloodFillBounds;

/* Fast neighbor expansion used for both main BFS and anchor drain.
 * Pushes unvisited solid neighbors onto the stack, marks visited+island_id.
 * Returns true if any neighbor outside bounds was solid (boundary anchor).
 * If anchored_ids is non-NULL, also checks visited neighbors for anchor
 * inheritance (sets *inherited_anchor = true). */
static bool expand_neighbors(const VoxelVolume *vol, ConnectivityWorkBuffer *work,
                             int32_t cx, int32_t cy, int32_t cz,
                             int32_t lx, int32_t ly, int32_t lz,
                             uint16_t island_id, const FloodFillBounds *bounds,
                             bool *stack_overflowed,
                             const bool *anchored_ids, bool *inherited_anchor)
{
    bool boundary_anchor = false;

    for (int32_t n = 0; n < 6; n++)
    {
        int32_t nx = lx + NEIGHBOR_OFFSETS[n][0];
        int32_t ny = ly + NEIGHBOR_OFFSETS[n][1];
        int32_t nz = lz + NEIGHBOR_OFFSETS[n][2];
        int32_t ncx = cx, ncy = cy, ncz = cz;

        if (nx < 0) { ncx--; nx = CHUNK_SIZE - 1; }
        else if (nx >= CHUNK_SIZE) { ncx++; nx = 0; }
        if (ny < 0) { ncy--; ny = CHUNK_SIZE - 1; }
        else if (ny >= CHUNK_SIZE) { ncy++; ny = 0; }
        if (nz < 0) { ncz--; nz = CHUNK_SIZE - 1; }
        else if (nz >= CHUNK_SIZE) { ncz++; nz = 0; }

        if (ncx < 0 || ncx >= vol->chunks_x ||
            ncy < 0 || ncy >= vol->chunks_y ||
            ncz < 0 || ncz >= vol->chunks_z)
            continue;

        if (bounds->bounded &&
            (ncx < bounds->min_cx || ncx > bounds->max_cx ||
             ncy < bounds->min_cy || ncy > bounds->max_cy ||
             ncz < bounds->min_cz || ncz > bounds->max_cz))
        {
            Chunk *nc = volume_get_chunk((VoxelVolume *)vol, ncx, ncy, ncz);
            if (nc && chunk_get(nc, nx, ny, nz) != 0)
                boundary_anchor = true;
            continue;
        }

        int32_t ngi = global_voxel_index(vol, ncx, ncy, ncz, nx, ny, nz);
        if (is_visited(work, ngi))
        {
            if (anchored_ids && inherited_anchor)
            {
                uint16_t nid = work->island_ids[ngi];
                if (nid > 0 && nid <= CONNECTIVITY_MAX_ISLANDS && anchored_ids[nid])
                    *inherited_anchor = true;
            }
            continue;
        }

        Chunk *nc = volume_get_chunk((VoxelVolume *)vol, ncx, ncy, ncz);
        if (!nc || chunk_get(nc, nx, ny, nz) == 0)
            continue;

        set_visited(work, ngi);
        set_island_id(work, ngi, island_id);

        if (work->stack_top < work->stack_capacity)
            work->stack[work->stack_top++] = pack_voxel_pos(ncx, ncy, ncz, nx, ny, nz);
        else
            *stack_overflowed = true;
    }

    return boundary_anchor;
}

static void flood_fill_island(const VoxelVolume *vol, ConnectivityWorkBuffer *work,
                              int32_t start_cx, int32_t start_cy, int32_t start_cz,
                              int32_t start_lx, int32_t start_ly, int32_t start_lz,
                              uint16_t island_id, IslandInfo *island, float anchor_y, uint8_t anchor_mat,
                              const FloodFillBounds *bounds, const bool *anchored_ids)
{
    work->stack_top = 0;
    bool stack_overflowed = false;

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

        if (world_pos.x < island->min_corner.x) island->min_corner.x = world_pos.x;
        if (world_pos.y < island->min_corner.y) island->min_corner.y = world_pos.y;
        if (world_pos.z < island->min_corner.z) island->min_corner.z = world_pos.z;
        if (world_pos.x > island->max_corner.x) island->max_corner.x = world_pos.x;
        if (world_pos.y > island->max_corner.y) island->max_corner.y = world_pos.y;
        if (world_pos.z > island->max_corner.z) island->max_corner.z = world_pos.z;

        int32_t gvx = cx * CHUNK_SIZE + lx;
        int32_t gvy = cy * CHUNK_SIZE + ly;
        int32_t gvz = cz * CHUNK_SIZE + lz;

        if (gvx < island->voxel_min_x) island->voxel_min_x = gvx;
        if (gvy < island->voxel_min_y) island->voxel_min_y = gvy;
        if (gvz < island->voxel_min_z) island->voxel_min_z = gvz;
        if (gvx > island->voxel_max_x) island->voxel_max_x = gvx;
        if (gvy > island->voxel_max_y) island->voxel_max_y = gvy;
        if (gvz > island->voxel_max_z) island->voxel_max_z = gvz;

        if (world_pos.y <= anchor_y + vol->voxel_size)
            island->anchor = ANCHOR_FLOOR;
        if (anchor_mat != 0 && mat == anchor_mat)
            island->anchor = ANCHOR_MATERIAL;

        bool inherited = false;
        bool boundary = expand_neighbors(vol, work, cx, cy, cz, lx, ly, lz,
                                         island_id, bounds, &stack_overflowed,
                                         anchored_ids, &inherited);
        if (boundary)
            island->anchor = ANCHOR_FLOOR;
        if (inherited)
            island->anchor = ANCHOR_FLOOR;

        /* Early anchor break: only when anchored_ids is provided (dirty
         * analysis path). Items on stack are already marked visited+island_id.
         * Future seeds will inherit anchor via anchored_ids lookup.
         * When anchored_ids is NULL (full volume scan), complete the BFS
         * to get accurate island counts and bounds. */
        if (anchored_ids && island->anchor != ANCHOR_NONE)
            break;
    }

    if (mass_sum > 0.0f)
    {
        island->center_of_mass = vec3_scale(com_sum, 1.0f / mass_sum);
        island->total_mass = mass_sum;
    }

    if (stack_overflowed)
        island->anchor = ANCHOR_FLOOR;

    island->is_floating = (island->anchor == ANCHOR_NONE);
}

/* Internal: scan a bounded region for islands, appending to result.
 * Does NOT clear work buffer or result — caller manages initialization.
 * next_island_id is passed by pointer so it persists across multiple calls. */
static void analyze_region_internal(const VoxelVolume *vol,
                                     Vec3 region_min, Vec3 region_max,
                                     float anchor_y, uint8_t anchor_material,
                                     ConnectivityWorkBuffer *work,
                                     ConnectivityResult *result,
                                     uint16_t *next_island_id)
{
    int32_t start_cx, start_cy, start_cz;
    int32_t end_cx, end_cy, end_cz;
    volume_world_to_chunk(vol, region_min, &start_cx, &start_cy, &start_cz);
    volume_world_to_chunk(vol, region_max, &end_cx, &end_cy, &end_cz);

    if (start_cx < 0) start_cx = 0;
    if (start_cy < 0) start_cy = 0;
    if (start_cz < 0) start_cz = 0;
    if (end_cx >= vol->chunks_x) end_cx = vol->chunks_x - 1;
    if (end_cy >= vol->chunks_y) end_cy = vol->chunks_y - 1;
    if (end_cz >= vol->chunks_z) end_cz = vol->chunks_z - 1;

    FloodFillBounds bounds = {
        .min_cx = start_cx, .min_cy = start_cy, .min_cz = start_cz,
        .max_cx = end_cx, .max_cy = end_cy, .max_cz = end_cz,
        .bounded = true
    };

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
                            island->island_id = *next_island_id;
                            island->min_corner = vec3_create(1e30f, 1e30f, 1e30f);
                            island->max_corner = vec3_create(-1e30f, -1e30f, -1e30f);
                            island->voxel_min_x = INT32_MAX;
                            island->voxel_min_y = INT32_MAX;
                            island->voxel_min_z = INT32_MAX;
                            island->voxel_max_x = INT32_MIN;
                            island->voxel_max_y = INT32_MIN;
                            island->voxel_max_z = INT32_MIN;

                            flood_fill_island(vol, work, cx, cy, cz, lx, ly, lz,
                                              *next_island_id, island, anchor_y, anchor_material,
                                              &bounds, NULL);

                            if (island->is_floating)
                                result->floating_count++;
                            else
                                result->anchored_count++;

                            result->island_count++;
                            (*next_island_id)++;
                        }
                    }
                }
            }
        }
    }
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

    uint16_t next_island_id = 1;
    analyze_region_internal(vol, region_min, region_max, anchor_y, anchor_material,
                            work, result, &next_island_id);
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

    memset(result, 0, sizeof(ConnectivityResult));

    if (vol->last_edit_count == 0)
    {
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    /* Decode dirty chunk coordinates */
    int32_t chunk_cx[VOLUME_EDIT_BATCH_MAX_CHUNKS];
    int32_t chunk_cy[VOLUME_EDIT_BATCH_MAX_CHUNKS];
    int32_t chunk_cz[VOLUME_EDIT_BATCH_MAX_CHUNKS];
    int32_t chunk_count = 0;

    for (int32_t i = 0; i < vol->last_edit_count; i++)
    {
        int32_t chunk_idx = vol->last_edit_chunks[i];
        if (chunk_idx < 0 || chunk_idx >= vol->total_chunks)
            continue;

        chunk_cx[chunk_count] = chunk_idx % vol->chunks_x;
        chunk_cy[chunk_count] = (chunk_idx / vol->chunks_x) % vol->chunks_y;
        chunk_cz[chunk_count] = chunk_idx / (vol->chunks_x * vol->chunks_y);
        chunk_count++;
    }

    if (chunk_count == 0)
    {
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    /* Cluster dirty chunks spatially using union-find.
     * Two chunks are in the same cluster if Chebyshev distance <= 2
     * (accounts for 1-chunk expansion on each side). */
    int32_t parent[VOLUME_EDIT_BATCH_MAX_CHUNKS];
    for (int32_t i = 0; i < chunk_count; i++)
        parent[i] = i;

    /* Find root with path compression */
    #define CLUSTER_FIND(p, x) \
        do { int32_t _r = (x); while ((p)[_r] != _r) { (p)[_r] = (p)[(p)[_r]]; _r = (p)[_r]; } (x) = _r; } while(0)

    for (int32_t i = 0; i < chunk_count; i++)
    {
        for (int32_t j = i + 1; j < chunk_count; j++)
        {
            int32_t dx = chunk_cx[i] - chunk_cx[j];
            int32_t dy = chunk_cy[i] - chunk_cy[j];
            int32_t dz = chunk_cz[i] - chunk_cz[j];
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dz < 0) dz = -dz;

            if (dx <= 2 && dy <= 2 && dz <= 2)
            {
                int32_t ri = i, rj = j;
                CLUSTER_FIND(parent, ri);
                CLUSTER_FIND(parent, rj);
                if (ri != rj)
                    parent[rj] = ri;
            }
        }
    }

    #undef CLUSTER_FIND

    /* Single clear for all clusters — fixes island_ids wipe between clusters */
    connectivity_work_clear(work);
    uint16_t next_island_id = 1;

    /* Track which island IDs are confirmed anchored. Subsequent BFS seeds
     * that encounter visited territory from an anchored island inherit
     * anchor status, avoiding false floating classification. */
    bool anchored_ids[CONNECTIVITY_MAX_ISLANDS + 2];
    memset(anchored_ids, 0, sizeof(anchored_ids));

    /* Unbounded BFS: each seed explores freely through the entire volume.
     * Anchor is detected by floor check (y <= anchor_y) or by inheritance
     * from adjacent anchored islands. BFS breaks immediately on anchor,
     * so anchored terrain BFS is fast. */
    FloodFillBounds bounds = { .bounded = false };

    /* Seed BFS from cut boundary voxels — only voxels adjacent to destroyed area.
     * This avoids scanning all 32K voxels per dirty chunk. If cut boundary
     * overflowed, fall back to scanning dirty chunks (rare worst case). */
    const CutBoundaryBuffer *boundary = &vol->last_cut_boundary;
    bool use_boundary_seeds = !boundary->overflow && boundary->count > 0;

    if (use_boundary_seeds)
    {
        /* Collect unique seed positions from cut boundary.
         * A seed may be stale (destroyed by later frames), so also check
         * neighbors of empty boundary voxels for solid seeds. */
        int32_t seed_packed[VOLUME_MAX_CUT_BOUNDARY * 2];
        int32_t seed_count = 0;

        for (int32_t i = 0; i < boundary->count; i++)
        {
            int32_t packed = boundary->packed_positions[i];
            int32_t cx = (packed >> 26) & 0x3F;
            int32_t cy = (packed >> 21) & 0x1F;
            int32_t cz = (packed >> 15) & 0x3F;
            int32_t lx = (packed >> 10) & 0x1F;
            int32_t ly = (packed >> 5) & 0x1F;
            int32_t lz = packed & 0x1F;

            if (cx < 0 || cx >= vol->chunks_x ||
                cy < 0 || cy >= vol->chunks_y ||
                cz < 0 || cz >= vol->chunks_z)
                continue;

            Chunk *chunk = volume_get_chunk((VoxelVolume *)vol, cx, cy, cz);
            if (!chunk)
                continue;

            if (chunk_get(chunk, lx, ly, lz) != 0)
            {
                if (seed_count < VOLUME_MAX_CUT_BOUNDARY * 2)
                    seed_packed[seed_count++] = packed;
            }
            else
            {
                /* Stale seed: destroyed by later frames. Check neighbors. */
                for (int32_t d = 0; d < 6; d++)
                {
                    int32_t nlx = lx + NEIGHBOR_OFFSETS[d][0];
                    int32_t nly = ly + NEIGHBOR_OFFSETS[d][1];
                    int32_t nlz = lz + NEIGHBOR_OFFSETS[d][2];
                    int32_t ncx = cx, ncy = cy, ncz = cz;

                    if (nlx < 0) { ncx--; nlx = CHUNK_SIZE - 1; }
                    else if (nlx >= CHUNK_SIZE) { ncx++; nlx = 0; }
                    if (nly < 0) { ncy--; nly = CHUNK_SIZE - 1; }
                    else if (nly >= CHUNK_SIZE) { ncy++; nly = 0; }
                    if (nlz < 0) { ncz--; nlz = CHUNK_SIZE - 1; }
                    else if (nlz >= CHUNK_SIZE) { ncz++; nlz = 0; }

                    if (ncx < 0 || ncx >= vol->chunks_x ||
                        ncy < 0 || ncy >= vol->chunks_y ||
                        ncz < 0 || ncz >= vol->chunks_z)
                        continue;

                    Chunk *nc = volume_get_chunk((VoxelVolume *)vol, ncx, ncy, ncz);
                    if (nc && chunk_get(nc, nlx, nly, nlz) != 0)
                    {
                        if (seed_count < VOLUME_MAX_CUT_BOUNDARY * 2)
                            seed_packed[seed_count++] = pack_voxel_pos(ncx, ncy, ncz, nlx, nly, nlz);
                    }
                }
            }
        }

        /* BFS from each unique seed */
        for (int32_t i = 0; i < seed_count; i++)
        {
            int32_t packed = seed_packed[i];
            int32_t cx = (packed >> 26) & 0x3F;
            int32_t cy = (packed >> 21) & 0x1F;
            int32_t cz = (packed >> 15) & 0x3F;
            int32_t lx = (packed >> 10) & 0x1F;
            int32_t ly = (packed >> 5) & 0x1F;
            int32_t lz = packed & 0x1F;


            int32_t global_idx = global_voxel_index(vol, cx, cy, cz, lx, ly, lz);

            if (is_visited(work, global_idx))
                continue;

            result->total_voxels_checked++;

            if (result->island_count >= CONNECTIVITY_MAX_ISLANDS)
                goto done;

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
                              next_island_id, island, anchor_y, anchor_material,
                              &bounds, anchored_ids);

            if (island->is_floating)
                result->floating_count++;
            else
            {
                result->anchored_count++;
                if (next_island_id <= CONNECTIVITY_MAX_ISLANDS)
                    anchored_ids[next_island_id] = true;
            }

            result->island_count++;
            next_island_id++;
        }
    }
    else
    {
        /* Fallback: scan dirty chunks when cut boundary overflowed */
        for (int32_t i = 0; i < chunk_count; i++)
        {
            int32_t cx = chunk_cx[i];
            int32_t cy = chunk_cy[i];
            int32_t cz = chunk_cz[i];

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
                            goto done;

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
                                          next_island_id, island, anchor_y, anchor_material,
                                          &bounds, anchored_ids);

                        if (island->is_floating)
                            result->floating_count++;
                        else
                        {
                            result->anchored_count++;
                            if (next_island_id <= CONNECTIVITY_MAX_ISLANDS)
                                anchored_ids[next_island_id] = true;
                        }

                        result->island_count++;
                        next_island_id++;
                    }
                }
            }
        }
    }

done:
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

    uint16_t target_id = (uint16_t)island->island_id;
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

    uint16_t target_id = (uint16_t)island->island_id;
    if (target_id == 0)
        return;

    volume_edit_begin(vol);
    vol->edit_budget_bypass = true;

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

static void boundary_bfs(const VoxelVolume *vol, ConnectivityWorkBuffer *work,
                          int32_t start_packed, uint16_t island_id,
                          IslandInfo *island, float anchor_y, uint8_t anchor_mat,
                          int32_t *budget_remaining, bool *found_anchor,
                          const bool *anchored_ids)
{
    *found_anchor = false;
    work->stack_top = 0;

    int32_t cx, cy, cz, lx, ly, lz;
    unpack_voxel_pos(start_packed, &cx, &cy, &cz, &lx, &ly, &lz);

    if (cx < 0 || cx >= vol->chunks_x ||
        cy < 0 || cy >= vol->chunks_y ||
        cz < 0 || cz >= vol->chunks_z)
        return;

    int32_t global_idx = global_voxel_index(vol, cx, cy, cz, lx, ly, lz);
    if (global_idx < 0 || global_idx >= work->visited_size)
        return;

    if (is_visited(work, global_idx))
        return;

    Chunk *chunk = volume_get_chunk((VoxelVolume *)vol, cx, cy, cz);
    if (!chunk || chunk_get(chunk, lx, ly, lz) == 0)
        return;

    set_visited(work, global_idx);
    set_island_id(work, global_idx, island_id);
    work->stack[work->stack_top++] = start_packed;

    island->min_corner = vec3_create(1e30f, 1e30f, 1e30f);
    island->max_corner = vec3_create(-1e30f, -1e30f, -1e30f);
    island->voxel_min_x = INT32_MAX;
    island->voxel_min_y = INT32_MAX;
    island->voxel_min_z = INT32_MAX;
    island->voxel_max_x = INT32_MIN;
    island->voxel_max_y = INT32_MIN;
    island->voxel_max_z = INT32_MIN;

    Vec3 com_sum = vec3_zero();
    float mass_sum = 0.0f;
    bool anchor_found = false;

    while (work->stack_top > 0 && *budget_remaining > 0)
    {
        int32_t packed = work->stack[--work->stack_top];
        unpack_voxel_pos(packed, &cx, &cy, &cz, &lx, &ly, &lz);
        (*budget_remaining)--;

        chunk = volume_get_chunk((VoxelVolume *)vol, cx, cy, cz);
        if (!chunk)
            continue;

        uint8_t mat = chunk_get(chunk, lx, ly, lz);
        if (mat == 0)
            continue;

        island->voxel_count++;

        Vec3 world_pos = volume_voxel_to_world(vol, cx, cy, cz, lx, ly, lz);

        com_sum = vec3_add(com_sum, world_pos);
        mass_sum += 1.0f;

        if (world_pos.x < island->min_corner.x) island->min_corner.x = world_pos.x;
        if (world_pos.y < island->min_corner.y) island->min_corner.y = world_pos.y;
        if (world_pos.z < island->min_corner.z) island->min_corner.z = world_pos.z;
        if (world_pos.x > island->max_corner.x) island->max_corner.x = world_pos.x;
        if (world_pos.y > island->max_corner.y) island->max_corner.y = world_pos.y;
        if (world_pos.z > island->max_corner.z) island->max_corner.z = world_pos.z;

        int32_t global_vx = cx * CHUNK_SIZE + lx;
        int32_t global_vy = cy * CHUNK_SIZE + ly;
        int32_t global_vz = cz * CHUNK_SIZE + lz;

        if (global_vx < island->voxel_min_x) island->voxel_min_x = global_vx;
        if (global_vy < island->voxel_min_y) island->voxel_min_y = global_vy;
        if (global_vz < island->voxel_min_z) island->voxel_min_z = global_vz;
        if (global_vx > island->voxel_max_x) island->voxel_max_x = global_vx;
        if (global_vy > island->voxel_max_y) island->voxel_max_y = global_vy;
        if (global_vz > island->voxel_max_z) island->voxel_max_z = global_vz;

        if (world_pos.y <= anchor_y + vol->voxel_size)
            anchor_found = true;
        if (anchor_mat != 0 && mat == anchor_mat)
            anchor_found = true;

        for (int32_t n = 0; n < 6; n++)
        {
            int32_t nx = lx + NEIGHBOR_OFFSETS[n][0];
            int32_t ny = ly + NEIGHBOR_OFFSETS[n][1];
            int32_t nz = lz + NEIGHBOR_OFFSETS[n][2];
            int32_t ncx = cx, ncy = cy, ncz = cz;

            if (nx < 0) { ncx--; nx = CHUNK_SIZE - 1; }
            else if (nx >= CHUNK_SIZE) { ncx++; nx = 0; }
            if (ny < 0) { ncy--; ny = CHUNK_SIZE - 1; }
            else if (ny >= CHUNK_SIZE) { ncy++; ny = 0; }
            if (nz < 0) { ncz--; nz = CHUNK_SIZE - 1; }
            else if (nz >= CHUNK_SIZE) { ncz++; nz = 0; }

            if (ncx < 0 || ncx >= vol->chunks_x ||
                ncy < 0 || ncy >= vol->chunks_y ||
                ncz < 0 || ncz >= vol->chunks_z)
                continue;

            int32_t neighbor_global = global_voxel_index(vol, ncx, ncy, ncz, nx, ny, nz);
            if (neighbor_global < 0 || neighbor_global >= work->visited_size)
                continue;

            if (is_visited(work, neighbor_global))
            {
                uint16_t nid = work->island_ids[neighbor_global];
                if (nid > 0 && nid <= CONNECTIVITY_MAX_ISLANDS && anchored_ids[nid])
                    anchor_found = true;
                continue;
            }

            Chunk *neighbor_chunk = volume_get_chunk((VoxelVolume *)vol, ncx, ncy, ncz);
            if (!neighbor_chunk)
                continue;
            if (chunk_get(neighbor_chunk, nx, ny, nz) == 0)
                continue;

            set_visited(work, neighbor_global);
            set_island_id(work, neighbor_global, island_id);

            if (work->stack_top < work->stack_capacity)
            {
                int32_t neighbor_packed = pack_voxel_pos(ncx, ncy, ncz, nx, ny, nz);
                work->stack[work->stack_top++] = neighbor_packed;
            }
        }

        /* Break AFTER neighbor expansion so the anchor voxel's neighbors
         * are all marked visited, creating wider coverage near the cut. */
        if (anchor_found)
        {
            /* Drain remaining stack: mark all queued voxels as visited with
             * this island_id so later seeds don't re-discover them as phantom
             * floating islands. No budget cost — just marking, not expanding. */
            while (work->stack_top > 0)
            {
                int32_t drain_packed = work->stack[--work->stack_top];
                int32_t dcx, dcy, dcz, dlx, dly, dlz;
                unpack_voxel_pos(drain_packed, &dcx, &dcy, &dcz, &dlx, &dly, &dlz);
                int32_t dgi = global_voxel_index(vol, dcx, dcy, dcz, dlx, dly, dlz);
                if (dgi >= 0 && dgi < work->visited_size)
                {
                    set_visited(work, dgi);
                    set_island_id(work, dgi, island_id);
                }
            }
            break;
        }
    }

    *found_anchor = anchor_found;

    if (mass_sum > 0.0f && !anchor_found)
    {
        island->center_of_mass = vec3_scale(com_sum, 1.0f / mass_sum);
        island->total_mass = mass_sum;
    }

    island->is_floating = !anchor_found;
}

void connectivity_analyze_boundary(const VoxelVolume *vol,
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

    memset(result, 0, sizeof(ConnectivityResult));

    const CutBoundaryBuffer *boundary = &vol->last_cut_boundary;

    /* If no cut boundary and no deferred work, check dirty chunks as fallback */
    if (boundary->count == 0 && !boundary->overflow && !work->has_deferred_work)
    {
        /* No destruction edits occurred — nothing to analyze */
        if (vol->last_edit_count == 0)
        {
            PROFILE_END(PROFILE_SIM_CONNECTIVITY);
            return;
        }
        /* Edits happened but no cut boundary (e.g., only additions).
         * Fall through to dirty chunk fallback. */
        connectivity_analyze_dirty(vol, anchor_y, anchor_material, work, result);
        PROFILE_END(PROFILE_SIM_CONNECTIVITY);
        return;
    }

    int32_t start_idx = 0;
    bool resuming = work->has_deferred_work;

    if (resuming)
    {
        start_idx = work->deferred_seed_start;
        work->has_deferred_work = false;
    }
    else
    {
        /* Fresh analysis: bump generation */
        work->generation++;
        if (work->generation == 0)
        {
            work->generation = 1;
            memset(work->visited_gen, 0, (size_t)work->visited_size * sizeof(uint16_t));
        }
    }

    uint16_t next_island_id = resuming ? work->deferred_next_island_id : 1;
    int32_t budget = CONNECTIVITY_BUDGET_PER_TICK;

    /* Use persistent anchored_ids from work buffer so anchor status
     * propagates correctly across deferred multi-frame analysis. */
    if (!resuming)
        memset(work->anchored_ids, 0, sizeof(work->anchored_ids));

    if (!boundary->overflow)
    {
        /* Fast path: seed from cut boundary voxels */
        int32_t i;
        for (i = start_idx; i < boundary->count && budget > 0; i++)
        {
            int32_t packed = boundary->packed_positions[i];

            int32_t cx, cy, cz, lx, ly, lz;
            unpack_voxel_pos(packed, &cx, &cy, &cz, &lx, &ly, &lz);

            if (cx < 0 || cx >= vol->chunks_x ||
                cy < 0 || cy >= vol->chunks_y ||
                cz < 0 || cz >= vol->chunks_z)
                continue;

            int32_t gi = global_voxel_index(vol, cx, cy, cz, lx, ly, lz);
            if (gi < 0 || gi >= work->visited_size)
                continue;
            if (is_visited(work, gi))
                continue;

            if (result->island_count >= CONNECTIVITY_MAX_ISLANDS)
                break;

            IslandInfo *island = &result->islands[result->island_count];
            memset(island, 0, sizeof(IslandInfo));
            island->island_id = next_island_id;

            bool found_anchor = false;
            boundary_bfs(vol, work, packed, next_island_id,
                         island, anchor_y, anchor_material,
                         &budget, &found_anchor, work->anchored_ids);

            if (island->voxel_count == 0)
                continue;

            if (!found_anchor && work->stack_top > 0)
            {
                /* Budget exhausted mid-BFS: island status is unknown.
                 * Conservatively treat as anchored to prevent false detachments.
                 * Remaining seeds will be deferred to next frame. */
                island->is_floating = false;
                island->anchor = ANCHOR_FLOOR;
                result->anchored_count++;
                if (next_island_id <= CONNECTIVITY_MAX_ISLANDS)
                    work->anchored_ids[next_island_id] = true;
            }
            else if (found_anchor)
            {
                island->is_floating = false;
                island->anchor = ANCHOR_FLOOR;
                result->anchored_count++;
                if (next_island_id <= CONNECTIVITY_MAX_ISLANDS)
                    work->anchored_ids[next_island_id] = true;

                /* Sweep remaining boundary seeds: mark any that are adjacent
                 * to visited-anchored territory as visited. This prevents
                 * thousands of redundant BFS calls on the anchored side of
                 * the cut, preserving budget for floating island detection. */
                for (int32_t j = i + 1; j < boundary->count; j++)
                {
                    int32_t sp = boundary->packed_positions[j];
                    int32_t scx, scy, scz, slx, sly, slz;
                    unpack_voxel_pos(sp, &scx, &scy, &scz, &slx, &sly, &slz);

                    if (scx < 0 || scx >= vol->chunks_x ||
                        scy < 0 || scy >= vol->chunks_y ||
                        scz < 0 || scz >= vol->chunks_z)
                        continue;

                    int32_t sgi = global_voxel_index(vol, scx, scy, scz, slx, sly, slz);
                    if (sgi < 0 || sgi >= work->visited_size)
                        continue;
                    if (is_visited(work, sgi))
                        continue;

                    Chunk *sc = volume_get_chunk((VoxelVolume *)vol, scx, scy, scz);
                    if (!sc || chunk_get(sc, slx, sly, slz) == 0)
                        continue;

                    for (int32_t n = 0; n < 6; n++)
                    {
                        int32_t snx = slx + NEIGHBOR_OFFSETS[n][0];
                        int32_t sny = sly + NEIGHBOR_OFFSETS[n][1];
                        int32_t snz = slz + NEIGHBOR_OFFSETS[n][2];
                        int32_t sncx = scx, sncy = scy, sncz = scz;

                        if (snx < 0) { sncx--; snx = CHUNK_SIZE - 1; }
                        else if (snx >= CHUNK_SIZE) { sncx++; snx = 0; }
                        if (sny < 0) { sncy--; sny = CHUNK_SIZE - 1; }
                        else if (sny >= CHUNK_SIZE) { sncy++; sny = 0; }
                        if (snz < 0) { sncz--; snz = CHUNK_SIZE - 1; }
                        else if (snz >= CHUNK_SIZE) { sncz++; snz = 0; }

                        if (sncx < 0 || sncx >= vol->chunks_x ||
                            sncy < 0 || sncy >= vol->chunks_y ||
                            sncz < 0 || sncz >= vol->chunks_z)
                            continue;

                        int32_t ngi = global_voxel_index(vol, sncx, sncy, sncz, snx, sny, snz);
                        if (ngi < 0 || ngi >= work->visited_size)
                            continue;
                        if (!is_visited(work, ngi))
                            continue;

                        uint16_t nid = work->island_ids[ngi];
                        if (nid > 0 && nid <= CONNECTIVITY_MAX_ISLANDS && work->anchored_ids[nid])
                        {
                            set_visited(work, sgi);
                            set_island_id(work, sgi, nid);
                            break;
                        }
                    }
                }
            }
            else
            {
                island->is_floating = true;
                island->anchor = ANCHOR_NONE;
                result->floating_count++;
            }

            result->island_count++;
            result->total_voxels_checked += island->voxel_count;
            next_island_id++;
        }

        /* If budget exhausted before processing all seeds, defer */
        if (i < boundary->count && budget <= 0)
        {
            work->deferred_seed_start = i;
            work->deferred_next_island_id = next_island_id;
            work->has_deferred_work = true;
        }
    }
    else
    {
        /* Overflow fallback: use dirty chunk analysis with early-anchor BFS.
         * Reuse existing dirty analysis but fix multi-cluster island_ids wipe. */
        connectivity_analyze_dirty(vol, anchor_y, anchor_material, work, result);
    }

    PROFILE_END(PROFILE_SIM_CONNECTIVITY);
}
