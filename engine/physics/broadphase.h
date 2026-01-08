#ifndef PATCH_PHYSICS_BROADPHASE_H
#define PATCH_PHYSICS_BROADPHASE_H

#include "engine/core/types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BROADPHASE_GRID_SIZE 32
#define BROADPHASE_TOTAL_CELLS (BROADPHASE_GRID_SIZE * BROADPHASE_GRID_SIZE * BROADPHASE_GRID_SIZE)
#define BROADPHASE_MAX_OBJECTS 8192
#define BROADPHASE_MAX_PAIRS 65536
#define BROADPHASE_MAX_PER_CELL 32

/*
 * Cell overflow policy: Objects are silently dropped when a cell exceeds
 * BROADPHASE_MAX_PER_CELL (32). This means collision detection may be
 * incomplete at extreme local densities. Practical limits: 32 objects
 * overlapping the same spatial cell is rare with typical object sizes
 * and cell_size derived from world bounds. If this becomes a bottleneck,
 * increase BROADPHASE_MAX_PER_CELL or reduce object clustering.
 */

    typedef struct
    {
        uint16_t object_id;
        uint16_t cell_index;
    } BroadphaseEntry;

    typedef struct
    {
        uint16_t a;
        uint16_t b;
    } CollisionPair;

    typedef struct
    {
        uint16_t objects[BROADPHASE_MAX_PER_CELL];
        uint8_t count;
    } BroadphaseCell;

    /* Pair deduplication using bit array (O(1) lookup instead of O(n)) */
    #define BROADPHASE_PAIR_HASH_SIZE 4096
    #define BROADPHASE_PAIR_HASH_MASK (BROADPHASE_PAIR_HASH_SIZE - 1)

    typedef struct
    {
        BroadphaseCell cells[BROADPHASE_TOTAL_CELLS];
        Bounds3D bounds;
        float cell_size;
        float inv_cell_size;

        CollisionPair pairs[BROADPHASE_MAX_PAIRS];
        int32_t pair_count;

        /* Bit array for O(1) pair deduplication */
        uint32_t pair_hash[BROADPHASE_PAIR_HASH_SIZE / 32];
    } BroadphaseGrid;

    static inline void broadphase_init(BroadphaseGrid *grid, Bounds3D bounds)
    {
        grid->bounds = bounds;

        float width = bounds.max_x - bounds.min_x;
        float height = bounds.max_y - bounds.min_y;
        float depth = bounds.max_z - bounds.min_z;
        float max_dim = (width > height) ? (width > depth ? width : depth) : (height > depth ? height : depth);

        grid->cell_size = max_dim / (float)BROADPHASE_GRID_SIZE;
        grid->inv_cell_size = 1.0f / grid->cell_size;
        grid->pair_count = 0;

        for (int32_t i = 0; i < BROADPHASE_TOTAL_CELLS; i++)
        {
            grid->cells[i].count = 0;
        }
    }

    static inline void broadphase_clear(BroadphaseGrid *grid)
    {
        for (int32_t i = 0; i < BROADPHASE_TOTAL_CELLS; i++)
        {
            grid->cells[i].count = 0;
        }
        grid->pair_count = 0;
        /* Clear pair hash for O(1) deduplication */
        for (int32_t i = 0; i < BROADPHASE_PAIR_HASH_SIZE / 32; i++)
        {
            grid->pair_hash[i] = 0;
        }
    }

    /* Hash function for pair deduplication */
    static inline uint32_t broadphase_pair_hash(uint16_t a, uint16_t b)
    {
        /* Combine both IDs into a single hash, order-independent since a < b */
        uint32_t combined = ((uint32_t)a << 16) | (uint32_t)b;
        /* Simple hash mixing */
        combined ^= combined >> 16;
        combined *= 0x85ebca6b;
        combined ^= combined >> 13;
        return combined & BROADPHASE_PAIR_HASH_MASK;
    }

    static inline void broadphase_pos_to_cell(const BroadphaseGrid *grid, Vec3 pos,
                                              int32_t *cx, int32_t *cy, int32_t *cz)
    {
        *cx = (int32_t)((pos.x - grid->bounds.min_x) * grid->inv_cell_size);
        *cy = (int32_t)((pos.y - grid->bounds.min_y) * grid->inv_cell_size);
        *cz = (int32_t)((pos.z - grid->bounds.min_z) * grid->inv_cell_size);

        if (*cx < 0)
            *cx = 0;
        if (*cy < 0)
            *cy = 0;
        if (*cz < 0)
            *cz = 0;
        if (*cx >= BROADPHASE_GRID_SIZE)
            *cx = BROADPHASE_GRID_SIZE - 1;
        if (*cy >= BROADPHASE_GRID_SIZE)
            *cy = BROADPHASE_GRID_SIZE - 1;
        if (*cz >= BROADPHASE_GRID_SIZE)
            *cz = BROADPHASE_GRID_SIZE - 1;
    }

    static inline int32_t broadphase_cell_index(int32_t cx, int32_t cy, int32_t cz)
    {
        return cx + cy * BROADPHASE_GRID_SIZE + cz * BROADPHASE_GRID_SIZE * BROADPHASE_GRID_SIZE;
    }

    static inline void broadphase_insert(BroadphaseGrid *grid, uint16_t object_id, Vec3 pos, float radius)
    {
        int32_t min_cx, min_cy, min_cz;
        int32_t max_cx, max_cy, max_cz;

        Vec3 min_pos = {pos.x - radius, pos.y - radius, pos.z - radius};
        Vec3 max_pos = {pos.x + radius, pos.y + radius, pos.z + radius};

        broadphase_pos_to_cell(grid, min_pos, &min_cx, &min_cy, &min_cz);
        broadphase_pos_to_cell(grid, max_pos, &max_cx, &max_cy, &max_cz);

        for (int32_t cz = min_cz; cz <= max_cz; cz++)
        {
            for (int32_t cy = min_cy; cy <= max_cy; cy++)
            {
                for (int32_t cx = min_cx; cx <= max_cx; cx++)
                {
                    int32_t cell_idx = broadphase_cell_index(cx, cy, cz);
                    BroadphaseCell *cell = &grid->cells[cell_idx];

                    if (cell->count < BROADPHASE_MAX_PER_CELL)
                    {
                        cell->objects[cell->count++] = object_id;
                    }
                }
            }
        }
    }

    /* O(1) pair existence check using hash bit array */
    static inline bool broadphase_pair_exists(const BroadphaseGrid *grid, uint16_t a, uint16_t b)
    {
        if (a > b)
        {
            uint16_t tmp = a;
            a = b;
            b = tmp;
        }

        uint32_t hash = broadphase_pair_hash(a, b);
        uint32_t word = hash / 32;
        uint32_t bit = hash % 32;
        return (grid->pair_hash[word] & (1u << bit)) != 0;
    }

    /* O(1) pair marking in hash bit array */
    static inline void broadphase_mark_pair(BroadphaseGrid *grid, uint16_t a, uint16_t b)
    {
        uint32_t hash = broadphase_pair_hash(a, b);
        uint32_t word = hash / 32;
        uint32_t bit = hash % 32;
        grid->pair_hash[word] |= (1u << bit);
    }

    static inline void broadphase_add_pair(BroadphaseGrid *grid, uint16_t a, uint16_t b)
    {
        if (a == b)
            return;
        if (grid->pair_count >= BROADPHASE_MAX_PAIRS)
            return;

        if (a > b)
        {
            uint16_t tmp = a;
            a = b;
            b = tmp;
        }

        /* O(1) deduplication check */
        if (broadphase_pair_exists(grid, a, b))
            return;

        /* Mark pair as seen */
        broadphase_mark_pair(grid, a, b);

        grid->pairs[grid->pair_count].a = a;
        grid->pairs[grid->pair_count].b = b;
        grid->pair_count++;
    }

    static inline void broadphase_generate_pairs(BroadphaseGrid *grid)
    {
        grid->pair_count = 0;
        /* Clear pair hash for fresh generation */
        for (int32_t i = 0; i < BROADPHASE_PAIR_HASH_SIZE / 32; i++)
        {
            grid->pair_hash[i] = 0;
        }

        for (int32_t cell_idx = 0; cell_idx < BROADPHASE_TOTAL_CELLS; cell_idx++)
        {
            BroadphaseCell *cell = &grid->cells[cell_idx];

            if (cell->count < 2)
                continue;

            /* Generate pairs within cell */
            for (uint8_t i = 0; i < cell->count; i++)
            {
                for (uint8_t j = i + 1; j < cell->count; j++)
                {
                    broadphase_add_pair(grid, cell->objects[i], cell->objects[j]);
                }
            }
        }
    }

    static inline void broadphase_sort_pairs(BroadphaseGrid *grid)
    {
        for (int32_t i = 1; i < grid->pair_count; i++)
        {
            CollisionPair key = grid->pairs[i];
            int32_t j = i - 1;

            while (j >= 0 &&
                   (grid->pairs[j].a > key.a ||
                    (grid->pairs[j].a == key.a && grid->pairs[j].b > key.b)))
            {
                grid->pairs[j + 1] = grid->pairs[j];
                j--;
            }
            grid->pairs[j + 1] = key;
        }
    }

#ifdef __cplusplus
}
#endif

#endif
