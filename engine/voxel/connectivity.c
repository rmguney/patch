#include "connectivity.h"
#include "content/materials.h"
#include "engine/core/math.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>

bool connectivity_work_init(ConnectivityWorkBuffer *work, const VoxelVolume *vol)
{
    if (!work || !vol)
        return false;

    memset(work, 0, sizeof(ConnectivityWorkBuffer));

    /* 2^32 voxels at VOLUME_MAX_CHUNKS: past int32 */
    size_t total_voxels = (size_t)vol->total_chunks * CHUNK_VOXEL_COUNT;

    /* Flood-fill stack sized to the volume, within fixed limits */
    int32_t stack_size = total_voxels > CONNECTIVITY_MAX_STACK_SIZE ? CONNECTIVITY_MAX_STACK_SIZE
                                                                    : (int32_t)total_voxels;
    if (stack_size < CONNECTIVITY_MIN_STACK_SIZE)
        stack_size = CONNECTIVITY_MIN_STACK_SIZE;

    work->stack = (int32_t *)malloc((size_t)stack_size * sizeof(int32_t));
    work->stack_capacity = stack_size;

    work->visited_size = total_voxels;
    work->visited_gen = (uint16_t *)calloc(total_voxels, sizeof(uint16_t));
    work->generation = 1;

    work->island_ids = (uint16_t *)calloc(total_voxels, sizeof(uint16_t));
    work->id_dirty_chunks = (int32_t *)malloc((size_t)vol->total_chunks * sizeof(int32_t));
    work->id_chunk_dirty = (uint8_t *)calloc((size_t)vol->total_chunks, sizeof(uint8_t));
    work->anchored_ids = (uint8_t *)calloc(CONNECTIVITY_MAX_ISLAND_IDS, sizeof(uint8_t));
    for (int32_t m = 0; m < VOXEL_MATERIAL_MAX; m++)
        work->joins[m] = m != MATERIAL_EMPTY && !material_is_liquid((uint8_t)m);

    if (!work->stack || !work->visited_gen || !work->island_ids ||
        !work->id_dirty_chunks || !work->id_chunk_dirty || !work->anchored_ids)
    {
        connectivity_work_destroy(work);
        return false;
    }
    return true;
}

void connectivity_work_destroy(ConnectivityWorkBuffer *work)
{
    if (!work)
        return;

    free(work->stack);
    free(work->visited_gen);
    free(work->island_ids);
    free(work->id_dirty_chunks);
    free(work->id_chunk_dirty);
    free(work->anchored_ids);
    memset(work, 0, sizeof(ConnectivityWorkBuffer));
}

/* Start a fresh analysis: new visited generation, island ids back to 0 */
static void work_clear(ConnectivityWorkBuffer *work)
{
    work->generation++;
    if (work->generation == 0)
    {
        /* Wrapped around: old stamps could match again, so reset them */
        work->generation = 1;
        memset(work->visited_gen, 0, work->visited_size * sizeof(uint16_t));
    }

    /* Clear only the chunk slices that received ids since the last clear. A full
     * memset is ~1.8 GB on the full scene and would run on every detach. */
    for (int32_t i = 0; i < work->id_dirty_count; i++)
    {
        int32_t chunk = work->id_dirty_chunks[i];
        memset(work->island_ids + (size_t)chunk * CHUNK_VOXEL_COUNT, 0,
               CHUNK_VOXEL_COUNT * sizeof(uint16_t));
        work->id_chunk_dirty[chunk] = 0;
    }
    work->id_dirty_count = 0;
    work->stack_top = 0;
}

typedef struct
{
    int32_t cx, cy, cz; /* Chunk */
    int32_t lx, ly, lz; /* Voxel within the chunk */
} VoxelPos;

static inline VoxelPos voxel_pos_unpack(int32_t packed)
{
    VoxelPos p;
    volume_unpack_voxel_pos(packed, &p.cx, &p.cy, &p.cz, &p.lx, &p.ly, &p.lz);
    return p;
}

static inline int32_t voxel_pos_pack(VoxelPos p)
{
    return volume_pack_voxel_pos(p.cx, p.cy, p.cz, p.lx, p.ly, p.lz);
}

static inline uint8_t voxel_at(const VoxelVolume *vol, VoxelPos p)
{
    return vol->chunks[volume_chunk_index(vol, p.cx, p.cy, p.cz)].voxels[chunk_voxel_index(p.lx, p.ly, p.lz)].material;
}

static inline size_t global_voxel_index(const VoxelVolume *vol, VoxelPos p)
{
    return (size_t)volume_chunk_index(vol, p.cx, p.cy, p.cz) * CHUNK_VOXEL_COUNT +
           (size_t)chunk_voxel_index(p.lx, p.ly, p.lz);
}

/* -y last: the flood fill pops the last pushed neighbour first, so it heads down to
 * the anchor floor before spreading sideways, which keeps anchored fills short */
static const int32_t NEIGHBOR_OFFSETS[6][3] = {
    {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};

/* Face neighbour n (0..5) of p across chunk borders; false outside the volume */
static bool voxel_neighbor(const VoxelVolume *vol, VoxelPos p, int32_t n, VoxelPos *out)
{
    VoxelPos q = p;
    q.lx += NEIGHBOR_OFFSETS[n][0];
    q.ly += NEIGHBOR_OFFSETS[n][1];
    q.lz += NEIGHBOR_OFFSETS[n][2];

    if (q.lx < 0) { q.cx--; q.lx = CHUNK_SIZE - 1; }
    else if (q.lx >= CHUNK_SIZE) { q.cx++; q.lx = 0; }
    if (q.ly < 0) { q.cy--; q.ly = CHUNK_SIZE - 1; }
    else if (q.ly >= CHUNK_SIZE) { q.cy++; q.ly = 0; }
    if (q.lz < 0) { q.cz--; q.lz = CHUNK_SIZE - 1; }
    else if (q.lz >= CHUNK_SIZE) { q.cz++; q.lz = 0; }

    if (q.cx < 0 || q.cx >= vol->chunks_x ||
        q.cy < 0 || q.cy >= vol->chunks_y ||
        q.cz < 0 || q.cz >= vol->chunks_z)
        return false;

    *out = q;
    return true;
}

static inline bool is_visited(const ConnectivityWorkBuffer *work, size_t global_idx)
{
    return work->visited_gen[global_idx] == work->generation;
}

/* Mark a voxel visited and owned by island_id */
static inline void claim_voxel(ConnectivityWorkBuffer *work, size_t global_idx, uint16_t island_id)
{
    work->visited_gen[global_idx] = work->generation;

    int32_t chunk = (int32_t)(global_idx / CHUNK_VOXEL_COUNT);
    if (!work->id_chunk_dirty[chunk])
    {
        work->id_chunk_dirty[chunk] = 1;
        work->id_dirty_chunks[work->id_dirty_count++] = chunk;
    }
    work->island_ids[global_idx] = island_id;
}

/* Flood-fill the component containing `start` until it proves anchored: a voxel at
 * the anchor floor, or contact with a component already found anchored (its voxels
 * keep that id in island_ids). A component that runs out without either is floating
 * and is fully explored, so its bounds and voxel count are exact. Voxels still on the
 * stack when an anchor is found are already claimed, so later seeds touching them
 * inherit the anchor instead of re-walking the component. */
static bool flood_fill_island(const VoxelVolume *vol, ConnectivityWorkBuffer *work,
                              VoxelPos start, uint16_t island_id, float anchor_y,
                              IslandInfo *island)
{
    const uint8_t *anchored_ids = work->anchored_ids;
    bool anchored = false;
    bool stack_overflowed = false;
    Vec3 com_sum = vec3_zero();

    work->stack_top = 0;
    work->stack[work->stack_top++] = voxel_pos_pack(start);
    claim_voxel(work, global_voxel_index(vol, start), island_id);

    while (work->stack_top > 0 && !anchored)
    {
        VoxelPos p = voxel_pos_unpack(work->stack[--work->stack_top]);

        island->voxel_count++;
        int32_t gvx = p.cx * CHUNK_SIZE + p.lx;
        int32_t gvy = p.cy * CHUNK_SIZE + p.ly;
        int32_t gvz = p.cz * CHUNK_SIZE + p.lz;
        Vec3 world_pos = volume_voxel_to_world(vol, gvx, gvy, gvz);
        com_sum = vec3_add(com_sum, world_pos);

        if (gvx < island->voxel_min_x) island->voxel_min_x = gvx;
        if (gvy < island->voxel_min_y) island->voxel_min_y = gvy;
        if (gvz < island->voxel_min_z) island->voxel_min_z = gvz;
        if (gvx > island->voxel_max_x) island->voxel_max_x = gvx;
        if (gvy > island->voxel_max_y) island->voxel_max_y = gvy;
        if (gvz > island->voxel_max_z) island->voxel_max_z = gvz;

        if (world_pos.y <= anchor_y + vol->voxel_size)
            anchored = true;

        for (int32_t n = 0; n < 6; n++)
        {
            VoxelPos q;
            if (!voxel_neighbor(vol, p, n, &q))
                continue;

            size_t ngi = global_voxel_index(vol, q);
            if (is_visited(work, ngi))
            {
                uint16_t nid = work->island_ids[ngi];
                if (nid > 0 && anchored_ids[nid])
                    anchored = true;
                continue;
            }
            if (!work->joins[voxel_at(vol, q)])
                continue;

            claim_voxel(work, ngi, island_id);
            if (work->stack_top < work->stack_capacity)
                work->stack[work->stack_top++] = voxel_pos_pack(q);
            else
                stack_overflowed = true;
        }
    }

    /* A component too big for the stack is treated as anchored: never detach
     * something that was not fully explored */
    if (stack_overflowed)
        anchored = true;

    if (!anchored)
        island->center_of_mass = vec3_scale(com_sum, 1.0f / (float)island->voxel_count);
    return !anchored;
}

/* Analyse the component through solid voxel `seed` unless an earlier seed already
 * covered it. Returns false when the result is full and analysis must stop. */
static bool analyze_seed(const VoxelVolume *vol, ConnectivityWorkBuffer *work, VoxelPos seed,
                         float anchor_y, uint16_t *next_island_id, ConnectivityResult *result)
{
    size_t seed_gi = global_voxel_index(vol, seed);
    if (is_visited(work, seed_gi))
        return true;

    /* A seed touching territory already proven anchored is anchored as well: claim
     * it with that id instead of spending a new id and a flood fill on it. Without
     * this a scan over large anchored chunks runs out of the 16-bit ids. */
    for (int32_t n = 0; n < 6; n++)
    {
        VoxelPos q;
        if (!voxel_neighbor(vol, seed, n, &q))
            continue;
        size_t ngi = global_voxel_index(vol, q);
        uint16_t nid = work->island_ids[ngi];
        if (is_visited(work, ngi) && nid > 0 && work->anchored_ids[nid])
        {
            claim_voxel(work, seed_gi, nid);
            return true;
        }
    }

    if (result->island_count >= CONNECTIVITY_MAX_ISLANDS || *next_island_id == UINT16_MAX)
    {
        result->truncated = true;
        return false;
    }

    uint16_t island_id = (*next_island_id)++;
    IslandInfo *island = &result->islands[result->island_count];
    memset(island, 0, sizeof(IslandInfo));
    island->island_id = island_id;
    island->voxel_min_x = island->voxel_min_y = island->voxel_min_z = INT32_MAX;
    island->voxel_max_x = island->voxel_max_y = island->voxel_max_z = INT32_MIN;

    /* Anchored components only spend an id, not a result slot, so many cut-boundary
     * seeds on the anchored side cannot crowd out the floating pieces */
    if (flood_fill_island(vol, work, seed, island_id, anchor_y, island))
        result->island_count++;
    else
        work->anchored_ids[island_id] = 1;
    return true;
}

/* Seed from every island voxel of one chunk. Returns false when analysis must stop. */
static bool analyze_chunk(const VoxelVolume *vol, ConnectivityWorkBuffer *work, int32_t chunk_idx,
                          float anchor_y, uint16_t *next_island_id, ConnectivityResult *result)
{
    const Chunk *chunk = &vol->chunks[chunk_idx];
    if (!chunk->occupancy.has_any)
        return true;

    VoxelPos p;
    p.cx = chunk_idx % vol->chunks_x;
    p.cy = (chunk_idx / vol->chunks_x) % vol->chunks_y;
    p.cz = chunk_idx / (vol->chunks_x * vol->chunks_y);
    for (p.lz = 0; p.lz < CHUNK_SIZE; p.lz++)
    {
        for (p.ly = 0; p.ly < CHUNK_SIZE; p.ly++)
        {
            for (p.lx = 0; p.lx < CHUNK_SIZE; p.lx++)
            {
                if (work->joins[chunk->voxels[chunk_voxel_index(p.lx, p.ly, p.lz)].material] &&
                    !analyze_seed(vol, work, p, anchor_y, next_island_id, result))
                    return false;
            }
        }
    }
    return true;
}

/* Seed from the island voxels next to removed ones. An entry may have been removed
 * by a later edit; its island neighbours are the seeds then. Returns false when
 * analysis must stop. */
static bool analyze_cut_boundary(const VoxelVolume *vol, ConnectivityWorkBuffer *work,
                                 const CutBoundaryBuffer *boundary, float anchor_y,
                                 uint16_t *next_island_id, ConnectivityResult *result)
{
    for (int32_t i = 0; i < boundary->count; i++)
    {
        VoxelPos p = voxel_pos_unpack(boundary->packed_positions[i]);
        if (work->joins[voxel_at(vol, p)])
        {
            if (!analyze_seed(vol, work, p, anchor_y, next_island_id, result))
                return false;
            continue;
        }
        for (int32_t n = 0; n < 6; n++)
        {
            VoxelPos q;
            if (voxel_neighbor(vol, p, n, &q) && work->joins[voxel_at(vol, q)] &&
                !analyze_seed(vol, work, q, anchor_y, next_island_id, result))
                return false;
        }
    }
    return true;
}

void connectivity_analyze_dirty(const VoxelVolume *vol, float anchor_y,
                                ConnectivityWorkBuffer *work, ConnectivityResult *result)
{
    if (!vol || !work || !result)
        return;

    memset(result, 0, sizeof(ConnectivityResult));
    if (vol->last_edit_count == 0)
        return;

    PROFILE_BEGIN(PROFILE_SIM_CONNECTIVITY);

    work_clear(work);
    memset(work->anchored_ids, 0, CONNECTIVITY_MAX_ISLAND_IDS * sizeof(uint8_t));
    uint16_t next_island_id = 1;

    const CutBoundaryBuffer *boundary = &vol->last_cut_boundary;
    if (!boundary->overflow && boundary->count > 0)
    {
        analyze_cut_boundary(vol, work, boundary, anchor_y, &next_island_id, result);
    }
    else if (vol->last_edit_overflow)
    {
        /* No usable cut boundary (it overflowed, or the edits only added voxels):
         * seed from every solid voxel of the edited chunks, all of which are in
         * the bitmap once the list overflowed */
        for (int32_t chunk_idx = 0; chunk_idx < vol->total_chunks; chunk_idx++)
        {
            if (((vol->last_edit_bitmap[chunk_idx >> 6] >> (chunk_idx & 63)) & 1) &&
                !analyze_chunk(vol, work, chunk_idx, anchor_y, &next_island_id, result))
                break;
        }
    }
    else
    {
        for (int32_t i = 0; i < vol->last_edit_count; i++)
        {
            if (!analyze_chunk(vol, work, vol->last_edit_chunks[i], anchor_y, &next_island_id, result))
                break;
        }
    }

    PROFILE_END(PROFILE_SIM_CONNECTIVITY);
}

int32_t connectivity_extract_island_with_ids(const VoxelVolume *vol,
                                             const IslandInfo *island,
                                             const ConnectivityWorkBuffer *work,
                                             uint8_t *out_voxels,
                                             int32_t out_size_x, int32_t out_size_y, int32_t out_size_z,
                                             Vec3 *out_origin)
{
    if (!vol || !island || !work || !out_voxels)
        return 0;

    int32_t size_x = island->voxel_max_x - island->voxel_min_x + 1;
    int32_t size_y = island->voxel_max_y - island->voxel_min_y + 1;
    int32_t size_z = island->voxel_max_z - island->voxel_min_z + 1;

    if (size_x > out_size_x || size_y > out_size_y || size_z > out_size_z)
        return 0;

    memset(out_voxels, 0, (size_t)out_size_x * (size_t)out_size_y * (size_t)out_size_z);

    if (out_origin)
    {
        out_origin->x = vol->bounds.min_x + (float)island->voxel_min_x * vol->voxel_size;
        out_origin->y = vol->bounds.min_y + (float)island->voxel_min_y * vol->voxel_size;
        out_origin->z = vol->bounds.min_z + (float)island->voxel_min_z * vol->voxel_size;
    }

    uint16_t target_id = (uint16_t)island->island_id;
    int32_t copied = 0;

    for (int32_t gz = island->voxel_min_z; gz <= island->voxel_max_z; gz++)
    {
        for (int32_t gy = island->voxel_min_y; gy <= island->voxel_max_y; gy++)
        {
            for (int32_t gx = island->voxel_min_x; gx <= island->voxel_max_x; gx++)
            {
                size_t gi = volume_voxel_linear_index(vol, gx, gy, gz);
                if (work->island_ids[gi] != target_id)
                    continue;

                uint8_t mat = vol->chunks[gi / CHUNK_VOXEL_COUNT].voxels[gi % CHUNK_VOXEL_COUNT].material;
                if (mat == MATERIAL_EMPTY)
                    continue;

                int32_t ox = gx - island->voxel_min_x;
                int32_t oy = gy - island->voxel_min_y;
                int32_t oz = gz - island->voxel_min_z;
                out_voxels[ox + oy * out_size_x + oz * out_size_x * out_size_y] = mat;
                copied++;
            }
        }
    }

    return copied;
}

void connectivity_remove_island(VoxelVolume *vol, const IslandInfo *island,
                                const ConnectivityWorkBuffer *work)
{
    if (!vol || !island || !work)
        return;

    uint16_t target_id = (uint16_t)island->island_id;
    if (target_id == 0)
        return;

    VoxelBox box = {island->voxel_min_x, island->voxel_min_y, island->voxel_min_z,
                    island->voxel_max_x, island->voxel_max_y, island->voxel_max_z};
    volume_remove_labelled(vol, &box, work->island_ids, target_id);
}
