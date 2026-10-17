#ifndef PATCH_GAME_WEATHER_H
#define PATCH_GAME_WEATHER_H

#include "content/scenes.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include "engine/sim/env_particles.h"
#include "game/biome.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        WEATHER_CLEAR,
        WEATHER_CLOUDY,
        WEATHER_STORM, /* Precipitation: snow where the climate is cold, rain elsewhere */
        WEATHER_COUNT
    } WeatherState;

    /* Precipitation is snow below this biome temperature: halfway between the climates of
     * the cold biomes (snow, taiga, about -0.7) and the temperate ones (about 0) */
#define WEATHER_SNOW_BELOW_TEMPERATURE -0.375f

    /* One weather for the whole world; what falls, and where snow builds up, follows the
     * local climate */
    typedef struct
    {
        WeatherState current;
        WeatherState target;
        float transition;      /* 0..1 blend from current to target; past 0.5 the target shows */
        float state_timer;     /* Seconds until the next state is picked */
        float intensity;       /* 0..1, eases toward the shown state's level */
        float storm_intensity; /* Level a storm eases to, rolled per state change */
        float snow_accumulation_timer;
        RngState rng; /* State changes and snow build-up: the same for every viewer */

        BiomeField climate;
        RngState precipitation_rng; /* Rain and snow particles, which follow the viewer */
        float precipitation_carry;  /* Fraction of a particle owed to the next step */
        bool snowing_at_viewer;
    } WeatherSystem;

    /* seed: the world seed, whose climate decides rain or snow */
    WeatherSystem weather_create(uint32_t seed);
    void weather_update(WeatherSystem *weather, float dt);
    /* Switches to a state immediately, at full intensity (debug controls) */
    void weather_set_state(WeatherSystem *weather, WeatherState state);
    /* Sets the wind and, in a storm, lets rain or snow fall around the viewer: particles
     * land within a few metres of it, each as rain or snow by the climate where it lands */
    void weather_apply_env_particles(WeatherSystem *weather, EnvParticleSystem *env, Vec3 viewer, float dt);
    /* During a heavy storm, now and then lays snow on open solid ground in cold climates.
     * Returns the voxels placed. */
    int32_t weather_accumulate_snow(WeatherSystem *weather, VoxelVolume *vol, float dt);
    void weather_apply_lighting(const WeatherSystem *weather, SceneLighting *lighting);

#ifdef __cplusplus
}
#endif

#endif
