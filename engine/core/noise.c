#include "engine/core/noise.h"
#include <math.h>

static float noise_hash(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t n = (uint32_t)x + (uint32_t)z * 57 + seed * 131;
    n = (n << 13) ^ n;
    return 1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF) / 1073741824.0f;
}

static float noise_smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float noise_value_2d(float x, float z, uint32_t seed)
{
    int32_t ix = (int32_t)floorf(x);
    int32_t iz = (int32_t)floorf(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;

    float v00 = noise_hash(ix, iz, seed);
    float v10 = noise_hash(ix + 1, iz, seed);
    float v01 = noise_hash(ix, iz + 1, seed);
    float v11 = noise_hash(ix + 1, iz + 1, seed);

    float sx = noise_smooth(fx);
    float sz = noise_smooth(fz);

    float nx0 = v00 + sx * (v10 - v00);
    float nx1 = v01 + sx * (v11 - v01);

    return nx0 + sz * (nx1 - nx0);
}

float noise_fbm_2d(float x, float z, uint32_t seed,
                    int32_t octaves, float lacunarity, float persistence)
{
    float value = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;

    for (int32_t i = 0; i < octaves; i++)
    {
        value += noise_value_2d(x * freq, z * freq, seed + (uint32_t)i * 1000) * amp;
        amp *= persistence;
        freq *= lacunarity;
    }

    return value;
}

float noise_billow_2d(float x, float z, uint32_t seed,
                       int32_t octaves, float lacunarity, float persistence)
{
    float value = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;

    for (int32_t i = 0; i < octaves; i++)
    {
        float n = noise_value_2d(x * freq, z * freq, seed + (uint32_t)i * 1000);
        value += (fabsf(n) * 2.0f - 1.0f) * amp;
        amp *= persistence;
        freq *= lacunarity;
    }

    return value;
}

float noise_ridged_2d(float x, float z, uint32_t seed,
                       int32_t octaves, float lacunarity, float persistence)
{
    float value = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float weight = 1.0f;

    for (int32_t i = 0; i < octaves; i++)
    {
        float n = noise_value_2d(x * freq, z * freq, seed + (uint32_t)i * 1000);
        float signal = 1.0f - fabsf(n);
        signal *= signal;
        signal *= weight;
        weight = fminf(1.0f, fmaxf(0.0f, signal * 2.0f));

        value += signal * amp;
        amp *= persistence;
        freq *= lacunarity;
    }

    return value;
}
