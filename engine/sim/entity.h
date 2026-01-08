#ifndef PATCH_SIM_ENTITY_H
#define PATCH_SIM_ENTITY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Entity handles are index+generation; fixed-capacity (no heap in sim). */

#define ENTITY_INVALID_ID 0
#define ENTITY_MAX_GENERATION 0xFFFF

    typedef uint32_t EntityId;

    static inline uint16_t entity_index(EntityId id)
    {
        return (uint16_t)(id & 0xFFFF);
    }

    static inline uint16_t entity_generation(EntityId id)
    {
        return (uint16_t)(id >> 16);
    }

    static inline EntityId entity_make_id(uint16_t index, uint16_t generation)
    {
        return ((uint32_t)generation << 16) | (uint32_t)index;
    }

    static inline bool entity_id_valid(EntityId id)
    {
        return id != ENTITY_INVALID_ID;
    }

    /* Slot generation is bumped on free so stale handles fail validation. */
    typedef struct
    {
        uint16_t generation;
        bool alive;
        uint8_t _pad;
    } EntitySlot;

/*
 * EntityPool manages a fixed-capacity set of slots.
 * Allocation is a bounded linear scan from a hint (cache-friendly).
 */
#define ENTITY_POOL_MAX_CAPACITY 4096

    typedef struct
    {
        EntitySlot slots[ENTITY_POOL_MAX_CAPACITY];
        uint16_t capacity;
        uint16_t count;
        uint16_t first_free_hint;
        uint16_t _pad;
    } EntityPool;

    /* Initialize entity pool with given capacity (must be <= ENTITY_POOL_MAX_CAPACITY) */
    static inline void entity_pool_init(EntityPool *pool, uint16_t capacity)
    {
        if (capacity > ENTITY_POOL_MAX_CAPACITY)
        {
            capacity = ENTITY_POOL_MAX_CAPACITY;
        }
        pool->capacity = capacity;
        pool->count = 0;
        pool->first_free_hint = 0;
        for (uint16_t i = 0; i < capacity; i++)
        {
            pool->slots[i].generation = 1;
            pool->slots[i].alive = false;
        }
    }

    static inline EntityId entity_pool_alloc(EntityPool *pool)
    {
        if (pool->count >= pool->capacity)
        {
            return ENTITY_INVALID_ID;
        }

        for (uint16_t i = pool->first_free_hint; i < pool->capacity; i++)
        {
            if (!pool->slots[i].alive)
            {
                pool->slots[i].alive = true;
                pool->count++;
                pool->first_free_hint = (uint16_t)(i + 1);
                return entity_make_id(i, pool->slots[i].generation);
            }
        }

        for (uint16_t i = 0; i < pool->first_free_hint; i++)
        {
            if (!pool->slots[i].alive)
            {
                pool->slots[i].alive = true;
                pool->count++;
                pool->first_free_hint = (uint16_t)(i + 1);
                return entity_make_id(i, pool->slots[i].generation);
            }
        }

        return ENTITY_INVALID_ID;
    }

    static inline bool entity_pool_free(EntityPool *pool, EntityId id)
    {
        if (id == ENTITY_INVALID_ID)
        {
            return false;
        }

        uint16_t index = entity_index(id);
        uint16_t gen = entity_generation(id);

        if (index >= pool->capacity)
        {
            return false;
        }

        EntitySlot *slot = &pool->slots[index];
        if (!slot->alive || slot->generation != gen)
        {
            return false;
        }

        slot->alive = false;
        slot->generation++;
        if (slot->generation == 0)
        {
            slot->generation = 1;
        }
        pool->count--;

        if (index < pool->first_free_hint)
        {
            pool->first_free_hint = index;
        }

        return true;
    }

    static inline bool entity_pool_alive(const EntityPool *pool, EntityId id)
    {
        if (id == ENTITY_INVALID_ID)
        {
            return false;
        }

        uint16_t index = entity_index(id);
        uint16_t gen = entity_generation(id);

        if (index >= pool->capacity)
        {
            return false;
        }

        const EntitySlot *slot = &pool->slots[index];
        return slot->alive && slot->generation == gen;
    }

    static inline int32_t entity_pool_get_index(const EntityPool *pool, EntityId id)
    {
        if (!entity_pool_alive(pool, id))
        {
            return -1;
        }
        return (int32_t)entity_index(id);
    }

    /* Reset pool, freeing all entities (generations preserved to invalidate old handles) */
    static inline void entity_pool_clear(EntityPool *pool)
    {
        for (uint16_t i = 0; i < pool->capacity; i++)
        {
            if (pool->slots[i].alive)
            {
                pool->slots[i].alive = false;
                pool->slots[i].generation++;
                if (pool->slots[i].generation == 0)
                {
                    pool->slots[i].generation = 1;
                }
            }
        }
        pool->count = 0;
        pool->first_free_hint = 0;
    }

    /*
     * Iteration helpers.
     * Usage:
     *   for (uint16_t i = 0; i < pool->capacity; i++) {
     *       if (!pool->slots[i].alive) continue;
     *       // process entity at index i
     *   }
     *
     * Or use the iterator macros:
     *   ENTITY_POOL_FOREACH(pool, index) {
     *       // process entity at index
     *   }
     */

#define ENTITY_POOL_FOREACH(pool, index_var)                                \
    for (uint16_t index_var = 0; index_var < (pool)->capacity; index_var++) \
        if ((pool)->slots[index_var].alive)

    /* Get EntityId for a known-valid index (useful during iteration) */
    static inline EntityId entity_pool_id_at(const EntityPool *pool, uint16_t index)
    {
        return entity_make_id(index, pool->slots[index].generation);
    }

#ifdef __cplusplus
}
#endif

#endif
