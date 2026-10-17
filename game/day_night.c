#include "day_night.h"
#include "engine/core/math.h"
#include <math.h>

/* sin(65 deg): highest sun, so noon shadows stay visible */
#define SUN_MAX_ELEVATION_SIN 0.906f
/* Azimuth of the noon sun; the sun turns a full circle a day, half of it between sunrise
 * (t = 0.25) and sunset (t = 0.75), so it rises and sets on opposite sides */
#define SUN_NOON_AZIMUTH 0.35f

DayNightCycle day_night_create(float cycle_duration_seconds)
{
    DayNightCycle cycle;
    cycle.time_of_day = 0.35f; /* Start at mid-morning */
    cycle.cycle_duration = cycle_duration_seconds;
    return cycle;
}

void day_night_update(DayNightCycle *cycle, float dt)
{
    if (cycle->cycle_duration <= 0.0f)
        return;
    cycle->time_of_day += dt / cycle->cycle_duration;
    if (cycle->time_of_day >= 1.0f)
        cycle->time_of_day -= 1.0f;
}

static float smoothstep_f(float edge0, float edge1, float x)
{
    float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Sine of the sun's elevation: peaks at noon (t=0.5), below the horizon at night */
static float sun_elevation(float time_of_day)
{
    return sinf((time_of_day - 0.25f) * 2.0f * K_PI);
}

/* Day/night factor of an elevation: 1.0 = full day, 0.0 = full night */
static float daylight_at(float elevation)
{
    return smoothstep_f(-0.1f, 0.2f, elevation);
}

float day_night_daylight(const DayNightCycle *cycle)
{
    return daylight_at(sun_elevation(cycle->time_of_day));
}

void day_night_apply_lighting(const DayNightCycle *cycle, SceneLighting *lighting)
{
    float t = cycle->time_of_day;

    float elevation = sun_elevation(t);
    float azimuth = SUN_NOON_AZIMUTH + (t - 0.5f) * 2.0f * K_PI;

    /* Sun direction in world space (unit length by construction): elevation follows the
     * day (capped), and the sun rests just above the horizon at night */
    float sin_elev = elevation > 0.01f ? fminf(elevation, SUN_MAX_ELEVATION_SIN) : 0.01f;
    float cos_elev = sqrtf(fmaxf(0.0f, 1.0f - sin_elev * sin_elev));
    float sin_az = sinf(azimuth);
    float cos_az = cosf(azimuth);

    Vec3 sun_dir = vec3_create(
        -cos_elev * sin_az,
        sin_elev,
        cos_elev * cos_az);

    float day_factor = daylight_at(elevation);

    /* Sunrise/sunset factor: peaks when sun is near horizon */
    float horizon_factor = 1.0f - fabsf(elevation * 2.0f);
    if (horizon_factor < 0.0f) horizon_factor = 0.0f;
    horizon_factor *= horizon_factor;

    /* Sun color: warm at horizon, white at zenith */
    Vec3 sun_white = vec3_create(1.0f, 0.98f, 0.95f);
    Vec3 sun_warm = vec3_create(1.0f, 0.6f, 0.3f);
    Vec3 sun_color = vec3_lerp(sun_white, sun_warm, horizon_factor);

    /* Sun strength dims at night */
    float sun_strength = day_factor * lerpf(1.0f, 0.7f, horizon_factor);

    /* Sky color */
    Vec3 sky_day = vec3_create(0.75f, 0.80f, 0.95f);
    Vec3 sky_sunset = vec3_create(0.9f, 0.55f, 0.35f);
    Vec3 sky_night = vec3_create(0.05f, 0.07f, 0.13f);
    Vec3 sky_color = vec3_lerp(sky_night, sky_day, day_factor);
    sky_color = vec3_lerp(sky_color, sky_sunset, horizon_factor * day_factor);

    /* Ambient: night values are moonlit, readable on a linear-to-display output */
    Vec3 sky_ambient_day = vec3_create(0.55f, 0.68f, 0.95f);
    Vec3 sky_ambient_night = vec3_create(0.20f, 0.25f, 0.42f);
    Vec3 sky_ambient = vec3_lerp(sky_ambient_night, sky_ambient_day, day_factor);

    Vec3 ground_ambient_day = vec3_create(0.38f, 0.32f, 0.28f);
    Vec3 ground_ambient_night = vec3_create(0.10f, 0.09f, 0.13f);
    Vec3 ground_ambient = vec3_lerp(ground_ambient_night, ground_ambient_day, day_factor);

    float ambient_strength = lerpf(0.25f, 0.32f, day_factor);

    /* Fill light follows the sun but offset; at night it doubles as cool moonlight */
    Vec3 fill_dir = vec3_create(
        sun_dir.x * 0.5f + 0.48f,
        sun_dir.y * 0.3f + 0.53f,
        sun_dir.z * 0.5f - 0.69f);
    fill_dir = vec3_normalize(fill_dir);

    float fill_strength = lerpf(0.18f, 0.25f, day_factor);
    Vec3 fill_color = vec3_lerp(vec3_create(0.3f, 0.35f, 0.5f), vec3_create(0.7f, 0.8f, 1.0f), day_factor);

    /* Back light */
    float back_strength = lerpf(0.04f, 0.12f, day_factor);

    /* Exposure: slightly dim at night */
    float exposure = lerpf(0.85f, 1.0f, day_factor);

    /* Emissive: brighter at night (glowing materials) */
    float emissive_mult = lerpf(4.0f, 2.0f, day_factor);

    /* Apply */
    lighting->sun_direction = sun_dir;
    lighting->sun_color = sun_color;
    lighting->sun_strength = sun_strength;
    lighting->fill_direction = fill_dir;
    lighting->fill_color = fill_color;
    lighting->fill_strength = fill_strength;
    lighting->back_strength = back_strength;
    lighting->sky_ambient_color = sky_ambient;
    lighting->ground_ambient_color = ground_ambient;
    lighting->ambient_strength = ambient_strength;
    lighting->sky_color = sky_color;
    lighting->exposure = exposure;
    lighting->emissive_multiplier = emissive_mult;
    lighting->rim_strength = lerpf(0.03f, 0.10f, day_factor);
}
