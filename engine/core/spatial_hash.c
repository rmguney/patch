#include "engine/core/spatial_hash.h"
#include "engine/core/math.h"
#include <string.h>

static inline uint32_t hash_cell(int32_t cx, int32_t cy, int32_t cz)
{
    /* Simple spatial hash combining cell coordinates */
    uint32_t h = ((uint32_t)cx * 73856093u) ^ ((uint32_t)cy * 19349663u) ^ ((uint32_t)cz * 83492791u);
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

int32_t spatial_hash_query(SpatialHashGrid *grid, Vec3 position, float radius,
                           int32_t *out_indices, int32_t max_results)
{
    /* A fresh generation marks objects seen by this query without clearing */
    grid->query_generation++;
    if (grid->query_generation == 0)
    {
        /* Wrapped: old stamps could equal the new generation, so reset them */
        memset(grid->object_seen_gen, 0, sizeof(grid->object_seen_gen));
        grid->query_generation = 1;
    }

    uint32_t gen = grid->query_generation;

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
                        grid->object_seen_gen[obj_idx] != gen)
                    {
                        grid->object_seen_gen[obj_idx] = gen;
                        out_indices[count++] = obj_idx;
                    }

                    entry_idx = grid->entries[entry_idx].next;
                }
            }
        }
    }

    return count;
}
