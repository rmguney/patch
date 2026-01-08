/* keeping this just in case for future alloc-free paths
 * connectivity analysis work buffers
 * physics contact pair lists
 * particle spawn batches
 * any per-frame scratch data
 */

#ifndef PATCH_CORE_ARENA_H
#define PATCH_CORE_ARENA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t *base;
        size_t size;
        size_t used;
    } Arena;

    static inline void arena_init(Arena *arena, void *buffer, size_t size)
    {
        arena->base = (uint8_t *)buffer;
        arena->size = size;
        arena->used = 0;
    }

    static inline void arena_reset(Arena *arena)
    {
        arena->used = 0;
    }

    static inline void *arena_alloc(Arena *arena, size_t size, size_t align)
    {
        size_t current = (size_t)arena->base + arena->used;
        size_t aligned = (current + align - 1) & ~(align - 1);
        size_t offset = aligned - (size_t)arena->base;

        if (offset + size > arena->size)
        {
            return NULL;
        }

        arena->used = offset + size;
        return (void *)aligned;
    }

    /* Convenience: allocate with default alignment (8 bytes) */
    static inline void *arena_push(Arena *arena, size_t size)
    {
        return arena_alloc(arena, size, 8);
    }

    /* Convenience: allocate array of count elements */
    static inline void *arena_push_array(Arena *arena, size_t count, size_t elem_size)
    {
        return arena_alloc(arena, count * elem_size, 8);
    }

    static inline size_t arena_remaining(const Arena *arena)
    {
        return arena->size - arena->used;
    }

    typedef struct
    {
        size_t used;
    } ArenaMark;

    static inline ArenaMark arena_mark(const Arena *arena)
    {
        ArenaMark m;
        m.used = arena->used;
        return m;
    }

    static inline void arena_restore(Arena *arena, ArenaMark mark)
    {
        arena->used = mark.used;
    }

#ifdef __cplusplus
}
#endif

#endif
