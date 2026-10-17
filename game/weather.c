#include "weather.h"
#include "content/materials.h"
#include "engine/core/math.h"

#define WEATHER_TRANSITION_SPEED 0.1f
#define WEATHER_MIN_DURATION 30.0f
#define WEATHER_MAX_DURATION 90.0f
#define WEATHER_INTENSITY_RATE 0.3f /* Intensity change per second */
#define WEATHER_CLOUDY_INTENSITY 0.2f
#define WEATHER_FORCED_STORM_INTENSITY 0.7f
#define WEATHER_FORCED_DURATION 60.0f
#define WEATHER_SEED 9999u
#define PRECIPITATION_SEED 19999u
#define SNOW_ACCUMULATION_INTERVAL 2.0f
#define SNOW_BUDGET_PER_CYCLE 64
/* Rain and snow land within PRECIPITATION_RADIUS metres (each way) of the viewer, falling
 * from PRECIPITATION_HEIGHT above it */
#define PRECIPITATION_RADIUS 16.0f
#define PRECIPITATION_HEIGHT 12.0f
/* Particles per second at full intensity. Snow falls an eighth as fast as rain, so it
 * takes far fewer flakes to fill the air as densely. */
#define RAIN_PER_SECOND 2000.0f
#define SNOW_PER_SECOND 300.0f

WeatherSystem weather_create(uint32_t seed)
{
    WeatherSystem w;
    w.current = WEATHER_CLEAR;
    w.target = WEATHER_CLEAR;
    w.transition = 1.0f;
    w.state_timer = 20.0f;
    w.intensity = 0.0f;
    w.storm_intensity = 0.0f;
    w.snow_accumulation_timer = 0.0f;
    rng_seed(&w.rng, seed + WEATHER_SEED);
    w.climate = biome_field_create(seed);
    rng_seed(&w.precipitation_rng, seed + PRECIPITATION_SEED);
    w.precipitation_carry = 0.0f;
    w.snowing_at_viewer = false;
    return w;
}

/* The state that is shown: the target once the blend is past halfway */
static WeatherState effective_state(const WeatherSystem *weather)
{
    return (weather->transition >= 0.5f) ? weather->target : weather->current;
}

static float state_intensity(const WeatherSystem *weather, WeatherState state)
{
    switch (state)
    {
    case WEATHER_STORM:
        return weather->storm_intensity;
    case WEATHER_CLOUDY:
        return WEATHER_CLOUDY_INTENSITY;
    default:
        return 0.0f;
    }
}

static WeatherState pick_next_state(RngState *rng, WeatherState current)
{
    float weights[WEATHER_COUNT];
    weights[WEATHER_CLEAR] = 3.0f;
    weights[WEATHER_CLOUDY] = 2.0f;
    weights[WEATHER_STORM] = 1.5f;

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

void weather_update(WeatherSystem *weather, float dt)
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
        weather->target = pick_next_state(&weather->rng, weather->current);
        if (weather->target != weather->current)
        {
            weather->transition = 0.0f;
            if (weather->target == WEATHER_STORM)
                weather->storm_intensity = rng_range_f32(&weather->rng, 0.5f, 0.8f);
        }
        weather->state_timer = rng_range_f32(&weather->rng, WEATHER_MIN_DURATION, WEATHER_MAX_DURATION);
    }

    /* Ease toward the shown state's level without overshooting it */
    float step = WEATHER_INTENSITY_RATE * dt;
    float diff = state_intensity(weather, effective_state(weather)) - weather->intensity;
    weather->intensity += clampf(diff, -step, step);
}

void weather_set_state(WeatherSystem *weather, WeatherState state)
{
    weather->current = state;
    weather->target = state;
    weather->transition = 1.0f;
    weather->state_timer = WEATHER_FORCED_DURATION;
    weather->storm_intensity = WEATHER_FORCED_STORM_INTENSITY;
    weather->intensity = state_intensity(weather, state);
}

static bool snows_at(const WeatherSystem *weather, float x, float z)
{
    return biome_field_temperature(&weather->climate, x, z) < WEATHER_SNOW_BELOW_TEMPERATURE;
}

void weather_apply_env_particles(WeatherSystem *weather, EnvParticleSystem *env, Vec3 viewer, float dt)
{
    if (!env)
        return;

    WeatherState effective = effective_state(weather);

    const Vec3 wind_dir = vec3_create(0.83205029f, 0.0f, 0.55470020f); /* normalize(0.3, 0, 0.2) */
    float wind_strength = weather->intensity * 2.0f;
    if (effective == WEATHER_STORM)
        wind_strength += 1.0f;
    env_particles_set_wind(env, wind_dir, wind_strength);

    if (effective != WEATHER_STORM)
    {
        weather->precipitation_carry = 0.0f;
        weather->snowing_at_viewer = false;
        return;
    }
    weather->snowing_at_viewer = snows_at(weather, viewer.x, viewer.z);

    /* Landing sites come at the rain rate; where it snows only the snow rate's share of
     * them is kept */
    weather->precipitation_carry += RAIN_PER_SECOND * weather->intensity * dt;
    const int32_t sites = (int32_t)weather->precipitation_carry;
    weather->precipitation_carry -= (float)sites;

    const Vec3 rain_velocity = env_particles_steady_velocity(env, ENV_PARTICLE_RAIN);
    const Vec3 snow_velocity = env_particles_steady_velocity(env, ENV_PARTICLE_SNOW);
    RngState *rng = &weather->precipitation_rng;
    for (int32_t i = 0; i < sites; i++)
    {
        float x = viewer.x + rng_range_f32(rng, -PRECIPITATION_RADIUS, PRECIPITATION_RADIUS);
        float z = viewer.z + rng_range_f32(rng, -PRECIPITATION_RADIUS, PRECIPITATION_RADIUS);
        bool snow = snows_at(weather, x, z);
        if (snow && rng_float(rng) >= SNOW_PER_SECOND / RAIN_PER_SECOND)
            continue;

        /* Falling at its steady velocity from upwind by its drift, so that it passes the
         * viewer's height over the landing site */
        Vec3 velocity = snow ? snow_velocity : rain_velocity;
        float fall_time = PRECIPITATION_HEIGHT / -velocity.y;
        Vec3 start = vec3_create(x - velocity.x * fall_time, viewer.y + PRECIPITATION_HEIGHT, z - velocity.z * fall_time);
        env_particles_spawn(env, snow ? ENV_PARTICLE_SNOW : ENV_PARTICLE_RAIN, start, velocity, rng);
    }
}

int32_t weather_accumulate_snow(WeatherSystem *weather, VoxelVolume *vol, float dt)
{
    if (!vol || effective_state(weather) != WEATHER_STORM || weather->intensity < 0.3f)
        return 0;

    weather->snow_accumulation_timer -= dt;
    if (weather->snow_accumulation_timer > 0.0f)
        return 0;

    weather->snow_accumulation_timer = SNOW_ACCUMULATION_INTERVAL;

    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    int32_t placed = 0;
    volume_edit_begin(vol);

    for (int32_t i = 0; i < SNOW_BUDGET_PER_CYCLE; i++)
    {
        float x = rng_range_f32(&weather->rng, vol->bounds.min_x, vol->bounds.max_x);
        float z = rng_range_f32(&weather->rng, vol->bounds.min_z, vol->bounds.max_z);
        int32_t gx, gy, gz;
        if (!snows_at(weather, x, z) || !volume_world_to_voxel(vol, vec3_create(x, vol->bounds.min_y, z), &gx, &gy, &gz))
            continue;

        /* One layer, on whatever solid tops the column (ground, leaves, lake ice); water
         * and flora stay clear */
        uint8_t top = MAT_AIR;
        gy = volume_column_top(vol, gx, gz, &top);
        if (gy < 0 || gy + 1 >= total_y || !material_is_solid(top) || top == MAT_SNOW)
            continue;

        volume_edit_set(vol, volume_voxel_to_world(vol, gx, gy + 1, gz), MAT_SNOW);
        placed++;
    }

    volume_edit_end(vol);
    return placed;
}

void weather_apply_lighting(const WeatherSystem *weather, SceneLighting *lighting)
{
    float cloud_factor = weather->intensity;
    WeatherState effective = effective_state(weather);

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

    /* Snowfall around the viewer: brighter ambient from snow reflection */
    if (effective == WEATHER_STORM && weather->snowing_at_viewer)
    {
        lighting->ground_ambient_color.x += 0.05f * weather->intensity;
        lighting->ground_ambient_color.y += 0.05f * weather->intensity;
        lighting->ground_ambient_color.z += 0.06f * weather->intensity;
    }
}
