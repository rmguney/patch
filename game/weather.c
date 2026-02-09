#include "weather.h"
#include "terrain_gen.h"
#include "content/materials.h"
#include <math.h>

#define WEATHER_TRANSITION_SPEED 0.1f
#define WEATHER_MIN_DURATION 30.0f
#define WEATHER_MAX_DURATION 90.0f
#define SNOW_ACCUMULATION_INTERVAL 2.0f
#define SNOW_BUDGET_PER_CYCLE 64

WeatherSystem weather_create(uint32_t seed)
{
    WeatherSystem w;
    w.current = WEATHER_CLEAR;
    w.target = WEATHER_CLEAR;
    w.transition = 1.0f;
    w.state_timer = 20.0f;
    w.intensity = 0.0f;
    w.snow_accumulation_timer = 0.0f;
    rng_seed(&w.rng, seed + 9999);
    return w;
}

static WeatherState pick_next_state(RngState *rng, WeatherState current, float temperature)
{
    /* Weighted transition table based on temperature */
    float weights[WEATHER_COUNT];
    weights[WEATHER_CLEAR] = 3.0f;
    weights[WEATHER_CLOUDY] = 2.0f;
    weights[WEATHER_RAIN] = (temperature > -0.2f) ? 1.5f : 0.2f;
    weights[WEATHER_SNOW] = (temperature < 0.0f) ? 2.0f : 0.0f;

    /* Don't pick same state twice in a row often */
    weights[current] *= 0.3f;

    float total = 0.0f;
    for (int32_t i = 0; i < WEATHER_COUNT; i++)
        total += weights[i];

    float roll = rng_float(rng) * total;
    float cumulative = 0.0f;
    for (int32_t i = 0; i < WEATHER_COUNT; i++)
    {
        cumulative += weights[i];
        if (roll <= cumulative)
            return (WeatherState)i;
    }
    return WEATHER_CLEAR;
}

void weather_update(WeatherSystem *weather, float dt, float temperature)
{
    /* Transition blend */
    if (weather->transition < 1.0f)
    {
        weather->transition += dt * WEATHER_TRANSITION_SPEED;
        if (weather->transition >= 1.0f)
        {
            weather->transition = 1.0f;
            weather->current = weather->target;
        }
    }

    /* State timer */
    weather->state_timer -= dt;
    if (weather->state_timer <= 0.0f && weather->transition >= 1.0f)
    {
        weather->target = pick_next_state(&weather->rng, weather->current, temperature);
        if (weather->target != weather->current)
            weather->transition = 0.0f;
        weather->state_timer = rng_range_f32(&weather->rng, WEATHER_MIN_DURATION, WEATHER_MAX_DURATION);
    }

    /* Intensity ramps based on state */
    float target_intensity = 0.0f;
    WeatherState effective = (weather->transition >= 0.5f) ? weather->target : weather->current;
    if (effective == WEATHER_RAIN || effective == WEATHER_SNOW)
        target_intensity = 0.5f + rng_float(&weather->rng) * 0.3f;
    else if (effective == WEATHER_CLOUDY)
        target_intensity = 0.2f;

    float blend_speed = 0.3f * dt;
    if (weather->intensity < target_intensity)
        weather->intensity += blend_speed;
    else
        weather->intensity -= blend_speed;
    if (weather->intensity < 0.0f) weather->intensity = 0.0f;
    if (weather->intensity > 1.0f) weather->intensity = 1.0f;
}

void weather_apply_env_particles(const WeatherSystem *weather, EnvParticleSystem *env)
{
    if (!env)
        return;

    WeatherState effective = (weather->transition >= 0.5f) ? weather->target : weather->current;

    float wind_strength = weather->intensity * 2.0f;
    Vec3 wind_dir = vec3_create(0.3f, 0.0f, 0.2f);
    float wind_len = sqrtf(wind_dir.x * wind_dir.x + wind_dir.z * wind_dir.z);
    if (wind_len > 0.001f)
    {
        wind_dir.x /= wind_len;
        wind_dir.z /= wind_len;
    }

    if (effective == WEATHER_RAIN || effective == WEATHER_SNOW)
        wind_strength += 1.0f;

    env_particles_set_wind(env, wind_dir, wind_strength);

    if (effective == WEATHER_RAIN)
        env_particles_set_weather(env, ENV_PARTICLE_DRIP, weather->intensity);
    else if (effective == WEATHER_SNOW)
        env_particles_set_weather(env, ENV_PARTICLE_SNOW, weather->intensity);
    else
        env_particles_set_weather(env, 0, 0.0f);
}

int32_t weather_accumulate_snow(WeatherSystem *weather, VoxelVolume *vol,
                                 float voxel_size, float amplitude, float frequency,
                                 uint32_t terrain_seed, float dt)
{
    WeatherState effective = (weather->transition >= 0.5f) ? weather->target : weather->current;
    if (effective != WEATHER_SNOW || weather->intensity < 0.3f)
        return 0;

    weather->snow_accumulation_timer -= dt;
    if (weather->snow_accumulation_timer > 0.0f)
        return 0;

    weather->snow_accumulation_timer = SNOW_ACCUMULATION_INTERVAL;

    int32_t placed = 0;
    volume_edit_begin(vol);

    for (int32_t i = 0; i < SNOW_BUDGET_PER_CYCLE; i++)
    {
        float x = rng_range_f32(&weather->rng, vol->bounds.min_x, vol->bounds.max_x);
        float z = rng_range_f32(&weather->rng, vol->bounds.min_z, vol->bounds.max_z);

        float surface_y = TERRAIN_BASE_HEIGHT + terrain_gen_height(x, z, amplitude, frequency, terrain_seed);

        if (surface_y <= TERRAIN_SEA_LEVEL)
            continue;

        Vec3 check_pos = vec3_create(x, surface_y + voxel_size, z);
        uint8_t above = volume_get_at(vol, check_pos);
        if (above != VOXEL_MATERIAL_EMPTY)
            continue;

        Vec3 surface_pos = vec3_create(x, surface_y, z);
        uint8_t surface_mat = volume_get_at(vol, surface_pos);
        if (surface_mat == MAT_SNOW || surface_mat == MAT_WATER || surface_mat == VOXEL_MATERIAL_EMPTY)
            continue;

        volume_edit_set(vol, check_pos, MAT_SNOW);
        placed++;
    }

    volume_edit_end(vol);
    return placed;
}

void weather_apply_lighting(const WeatherSystem *weather, SceneLighting *lighting)
{
    float cloud_factor = weather->intensity;
    WeatherState effective = (weather->transition >= 0.5f) ? weather->target : weather->current;

    if (effective == WEATHER_CLEAR)
        cloud_factor *= 0.1f;
    else if (effective == WEATHER_CLOUDY)
        cloud_factor *= 0.5f;

    /* Dim sun through clouds */
    lighting->sun_strength *= (1.0f - cloud_factor * 0.6f);

    /* Increase ambient (light scattering) */
    lighting->ambient_strength += cloud_factor * 0.1f;

    /* Desaturate sky slightly */
    float desat = cloud_factor * 0.3f;
    float avg = (lighting->sky_color.x + lighting->sky_color.y + lighting->sky_color.z) / 3.0f;
    lighting->sky_color.x += (avg - lighting->sky_color.x) * desat;
    lighting->sky_color.y += (avg - lighting->sky_color.y) * desat;
    lighting->sky_color.z += (avg - lighting->sky_color.z) * desat;

    /* Snow: brighter ambient from snow reflection */
    if (effective == WEATHER_SNOW)
    {
        lighting->ground_ambient_color.x += 0.05f * weather->intensity;
        lighting->ground_ambient_color.y += 0.05f * weather->intensity;
        lighting->ground_ambient_color.z += 0.06f * weather->intensity;
    }
}
