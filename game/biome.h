#ifndef PATCH_GAME_BIOME_H
#define PATCH_GAME_BIOME_H

#include "engine/core/noise.h"
#include "content/materials.h"
#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BIOME_TEMP_SEED_OFFSET  3000
#define BIOME_HUM_SEED_OFFSET   4000
#define BIOME_TEMP_SCALE        0.1f
#define BIOME_HUM_SCALE         0.15f

    typedef enum
    {
        BIOME_GRASSLAND,
        BIOME_FOREST,
        BIOME_DESERT,
        BIOME_SNOW,
        BIOME_JUNGLE,
        BIOME_SWAMP,
        BIOME_SAVANNAH,
        BIOME_TAIGA,
        BIOME_MOUNTAIN,
        BIOME_COUNT
    } BiomeKind;

    static inline float biome_closeness(float value, float center, float tolerance)
    {
        if (tolerance <= 0.0f)
            return 1.0f;
        float dist = fabsf(value - center);
        if (dist >= tolerance)
            return 0.0f;
        float t = 1.0f - dist / tolerance;
        return t * t;
    }

    static inline float biome_temperature(float x, float z, uint32_t seed)
    {
        return noise_value_2d(x * BIOME_TEMP_SCALE, z * BIOME_TEMP_SCALE,
                              seed + BIOME_TEMP_SEED_OFFSET);
    }

    static inline float biome_humidity(float x, float z, uint32_t seed)
    {
        return noise_value_2d(x * BIOME_HUM_SCALE, z * BIOME_HUM_SCALE,
                              seed + BIOME_HUM_SEED_OFFSET);
    }

    static inline BiomeKind biome_classify(float temp, float humidity, float altitude)
    {
        if (altitude > 0.6f)
            return BIOME_MOUNTAIN;
        if (temp < -0.4f)
            return BIOME_SNOW;
        if (temp < -0.1f && humidity > 0.2f)
            return BIOME_TAIGA;
        if (temp > 0.5f && humidity < 0.2f)
            return BIOME_DESERT;
        if (temp > 0.5f && humidity > 0.6f)
            return BIOME_JUNGLE;
        if (humidity > 0.6f)
            return BIOME_SWAMP;
        if (humidity > 0.4f)
            return BIOME_FOREST;
        if (temp > 0.3f && humidity >= 0.2f && humidity <= 0.4f)
            return BIOME_SAVANNAH;
        return BIOME_GRASSLAND;
    }

    static inline uint8_t biome_surface_material(BiomeKind biome)
    {
        switch (biome)
        {
        case BIOME_DESERT:    return MAT_SAND;
        case BIOME_SNOW:      return MAT_SNOW;
        case BIOME_TAIGA:     return MAT_SNOW;
        case BIOME_MOUNTAIN:  return MAT_STONE;
        default:              return MAT_GRASS;
        }
    }

    static inline uint8_t biome_subsurface_material(BiomeKind biome)
    {
        switch (biome)
        {
        case BIOME_DESERT:    return MAT_SAND;
        case BIOME_MOUNTAIN:  return MAT_STONE;
        case BIOME_SWAMP:     return MAT_DIRT;
        default:              return MAT_DIRT;
        }
    }

#ifdef __cplusplus
}
#endif

#endif
