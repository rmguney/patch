#ifndef PATCH_ENGINE_CORE_NOISE_H
#define PATCH_ENGINE_CORE_NOISE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    float noise_value_2d(float x, float z, uint32_t seed);

    float noise_fbm_2d(float x, float z, uint32_t seed,
                        int32_t octaves, float lacunarity, float persistence);

    float noise_billow_2d(float x, float z, uint32_t seed,
                           int32_t octaves, float lacunarity, float persistence);

    float noise_ridged_2d(float x, float z, uint32_t seed,
                           int32_t octaves, float lacunarity, float persistence);

#ifdef __cplusplus
}
#endif

#endif
