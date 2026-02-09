#ifndef PATCH_GAME_WEATHER_H
#define PATCH_GAME_WEATHER_H

#include "content/scenes.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include "engine/sim/env_particles.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        WEATHER_CLEAR,
        WEATHER_CLOUDY,
        WEATHER_RAIN,
        WEATHER_SNOW,
        WEATHER_COUNT
    } WeatherState;

    typedef struct
    {
        WeatherState current;
        WeatherState target;
        float transition;
        float state_timer;
        float intensity;
        float snow_accumulation_timer;
        RngState rng;
    } WeatherSystem;

    WeatherSystem weather_create(uint32_t seed);
    void weather_update(WeatherSystem *weather, float dt, float temperature);
    void weather_apply_env_particles(const WeatherSystem *weather, EnvParticleSystem *env);
    int32_t weather_accumulate_snow(WeatherSystem *weather, VoxelVolume *vol,
                                    float voxel_size, float amplitude, float frequency,
                                    uint32_t terrain_seed, float dt);
    void weather_apply_lighting(const WeatherSystem *weather, SceneLighting *lighting);

#ifdef __cplusplus
}
#endif

#endif
