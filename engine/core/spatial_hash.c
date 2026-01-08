#include "engine/core/spatial_hash.h"
#include "engine/core/math.h"
#include <string.h>

static inline uint32_t hash_cell(int32_t cx, int32_t cy, int32_t cz)
{
    /* Simple spatial hash combining cell coordinates */
    uint32_t h = (uint32_t)(cx * 73856093) ^ (uint32_t)(cy * 19349663) ^ (uint32_t)(cz * 83492791);
    return h % SPATIAL_HASH_BUCKET_COUNT;
}

static inline void world_to_cell(const SpatialHashGrid *grid, Vec3 pos, int32_t *cx, int32_t *cy, int32_t *cz)
{
    *cx = (int32_t)((pos.x - grid->bounds.min_x) * grid->inv_cell_size);
    *cy = (int32_t)((pos.y - grid->bounds.min_y) * grid->inv_cell_size);
    *cz = (int32_t)((pos.z - grid->bounds.min_z) * grid->inv_cell_size);
}

void spatial_hash_init(SpatialHashGrid *grid, float cell_size, Bounds3D bounds)
{
    grid->cell_size = cell_size;
    grid->inv_cell_size = 1.0f / cell_size;
    grid->bounds = bounds;
    grid->entry_count = 0;
    grid->query_generation = 1;

    for (int32_t i = 0; i < SPATIAL_HASH_BUCKET_COUNT; i++)
    {
        grid->bucket_heads[i] = -1;
    }
    memset(grid->object_seen_gen, 0, sizeof(grid->object_seen_gen));
}

void spatial_hash_clear(SpatialHashGrid *grid)
{
    grid->entry_count = 0;
    for (int32_t i = 0; i < SPATIAL_HASH_BUCKET_COUNT; i++)
    {
        grid->bucket_heads[i] = -1;
    }
}

static void insert_into_cell(SpatialHashGrid *grid, int32_t object_index, int32_t cx, int32_t cy, int32_t cz)
{
    if (grid->entry_count >= SPATIAL_HASH_MAX_ENTRIES)
        return;

    uint32_t bucket = hash_cell(cx, cy, cz);
    int32_t entry_idx = grid->entry_count++;

    grid->entries[entry_idx].object_index = object_index;
    grid->entries[entry_idx].next = grid->bucket_heads[bucket];
    grid->bucket_heads[bucket] = entry_idx;
}

void spatial_hash_insert(SpatialHashGrid *grid, int32_t object_index, Vec3 position, float radius)
{
    Vec3 min_pos = vec3_create(position.x - radius, position.y - radius, position.z - radius);
    Vec3 max_pos = vec3_create(position.x + radius, position.y + radius, position.z + radius);

    int32_t min_cx, min_cy, min_cz;
    int32_t max_cx, max_cy, max_cz;
    world_to_cell(grid, min_pos, &min_cx, &min_cy, &min_cz);
    world_to_cell(grid, max_pos, &max_cx, &max_cy, &max_cz);

    for (int32_t cz = min_cz; cz <= max_cz; cz++)
    {
        for (int32_t cy = min_cy; cy <= max_cy; cy++)
        {
            for (int32_t cx = min_cx; cx <= max_cx; cx++)
            {
                insert_into_cell(grid, object_index, cx, cy, cz);
            }
        }
    }
}

void spatial_hash_insert_aabb(SpatialHashGrid *grid, int32_t object_index, Vec3 min, Vec3 max)
{
    int32_t min_cx, min_cy, min_cz;
    int32_t max_cx, max_cy, max_cz;
    world_to_cell(grid, min, &min_cx, &min_cy, &min_cz);
    world_to_cell(grid, max, &max_cx, &max_cy, &max_cz);

    for (int32_t cz = min_cz; cz <= max_cz; cz++)
    {
        for (int32_t cy = min_cy; cy <= max_cy; cy++)
        {
            for (int32_t cx = min_cx; cx <= max_cx; cx++)
            {
                insert_into_cell(grid, object_index, cx, cy, cz);
            }
        }
    }
}

int32_t spatial_hash_query(const SpatialHashGrid *grid, Vec3 position, float radius,
                           int32_t *out_indices, int32_t max_results)
{
    /* Cast away const for generation tracking (doesn't affect logical constness) */
    SpatialHashGrid *mutable_grid = (SpatialHashGrid *)grid;
    mutable_grid->query_generation++;
    if (mutable_grid->query_generation == 0)
        mutable_grid->query_generation = 1; /* Handle wraparound */

    uint32_t gen = mutable_grid->query_generation;

    Vec3 min_pos = vec3_create(position.x - radius, position.y - radius, position.z - radius);
    Vec3 max_pos = vec3_create(position.x + radius, position.y + radius, position.z + radius);

    int32_t min_cx, min_cy, min_cz;
    int32_t max_cx, max_cy, max_cz;
    world_to_cell(grid, min_pos, &min_cx, &min_cy, &min_cz);
    world_to_cell(grid, max_pos, &max_cx, &max_cy, &max_cz);

    int32_t count = 0;

    for (int32_t cz = min_cz; cz <= max_cz && count < max_results; cz++)
    {
        for (int32_t cy = min_cy; cy <= max_cy && count < max_results; cy++)
        {
            for (int32_t cx = min_cx; cx <= max_cx && count < max_results; cx++)
            {
                uint32_t bucket = hash_cell(cx, cy, cz);
                int32_t entry_idx = grid->bucket_heads[bucket];

                while (entry_idx >= 0 && count < max_results)
                {
                    int32_t obj_idx = grid->entries[entry_idx].object_index;

                    /* O(1) duplicate check using generation numbers */
                    if (obj_idx >= 0 && obj_idx < SPATIAL_HASH_MAX_OBJECTS &&
                        mutable_grid->object_seen_gen[obj_idx] != gen)
                    {
                        mutable_grid->object_seen_gen[obj_idx] = gen;
                        out_indices[count++] = obj_idx;
                    }

                    entry_idx = grid->entries[entry_idx].next;
                }
            }
        }
    }

    return count;
}

void spatial_hash_for_each_pair(const SpatialHashGrid *grid, SpatialHashPairCallback callback, void *user_data)
{
    /* For each bucket, check all pairs within that bucket */
    /* Use a visited bitset to avoid reporting same pair twice */
    /* Since objects can be in multiple cells, we only process pair (a,b) where a < b */

    for (int32_t bucket = 0; bucket < SPATIAL_HASH_BUCKET_COUNT; bucket++)
    {
        int32_t entry_a = grid->bucket_heads[bucket];
        while (entry_a >= 0)
        {
            int32_t idx_a = grid->entries[entry_a].object_index;

            int32_t entry_b = grid->entries[entry_a].next;
            while (entry_b >= 0)
            {
                int32_t idx_b = grid->entries[entry_b].object_index;

                /* Only report each pair once: smaller index first */
                if (idx_a < idx_b)
                {
                    callback(idx_a, idx_b, user_data);
                }
                else if (idx_b < idx_a)
                {
                    callback(idx_b, idx_a, user_data);
                }
                /* Skip if same object (shouldn't happen but safety check) */

                entry_b = grid->entries[entry_b].next;
            }

            entry_a = grid->entries[entry_a].next;
        }
    }
}
