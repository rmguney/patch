#ifndef PATCH_CORE_RNG_H
#define PATCH_CORE_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * RNG using xorshift64.
     * Fast, simple, good distribution for gameplay randomness.
     */

    typedef struct
    {
        uint64_t state;
    } RngState;

    static inline void rng_seed(RngState *rng, uint64_t seed)
    {
        rng->state = seed ? seed : 1;
    }

    static inline uint64_t rng_next(RngState *rng)
    {
        uint64_t x = rng->state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng->state = x;
        return x;
    }

    static inline uint32_t rng_range_u32(RngState *rng, uint32_t max)
    {
        if (max == 0)
            return 0;
        return (uint32_t)(rng_next(rng) % max);
    }

    static inline int32_t rng_range_i32(RngState *rng, int32_t min, int32_t max)
    {
        if (max <= min)
            return min;
        uint32_t range = (uint32_t)(max - min + 1);
        return min + (int32_t)rng_range_u32(rng, range);
    }

    static inline float rng_float(RngState *rng)
    {
        return (float)(rng_next(rng) & 0xFFFFFF) / 16777216.0f;
    }

    static inline float rng_range_f32(RngState *rng, float min, float max)
    {
        return min + rng_float(rng) * (max - min);
    }

    static inline float rng_signed_half(RngState *rng)
    {
        return rng_float(rng) - 0.5f;
    }

#ifdef __cplusplus
}
#endif

#endif
