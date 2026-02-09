#include "day_night.h"
#include "engine/core/math.h"
#include <math.h>

#define PI_F 3.14159265f
#define TWO_PI_F 6.28318530f

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
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static Vec3 vec3_lerp_f(Vec3 a, Vec3 b, float t)
{
    return vec3_create(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t);
}

static float lerp_f(float a, float b, float t)
{
    return a + (b - a) * t;
}

void day_night_apply_lighting(const DayNightCycle *cycle, SceneLighting *lighting)
{
    float t = cycle->time_of_day;

    /* Sun elevation: peaks at noon (t=0.5), below horizon at night */
    float sun_angle = (t - 0.25f) * TWO_PI_F;
    float elevation = sinf(sun_angle);
    float azimuth = 0.35f + t * 0.2f;

    /* Sun direction in world space */
    float cos_elev = cosf(acosf(fabsf(elevation) < 1.0f ? elevation : (elevation > 0.0f ? 1.0f : -1.0f)));
    if (elevation < 0.0f) cos_elev = -cos_elev;
    float sin_az = sinf(azimuth);
    float cos_az = cosf(azimuth);

    Vec3 sun_dir = vec3_create(
        -cos_elev * sin_az,
        elevation > 0.0f ? elevation : 0.01f,
        cos_elev * cos_az);
    float len = sqrtf(sun_dir.x * sun_dir.x + sun_dir.y * sun_dir.y + sun_dir.z * sun_dir.z);
    if (len > 0.001f)
    {
        sun_dir.x /= len;
        sun_dir.y /= len;
        sun_dir.z /= len;
    }

    /* Day/night factor: 1.0 = full day, 0.0 = full night */
    float day_factor = smoothstep_f(-0.1f, 0.2f, elevation);

    /* Sunrise/sunset factor: peaks when sun is near horizon */
    float horizon_factor = 1.0f - fabsf(elevation * 2.0f);
    if (horizon_factor < 0.0f) horizon_factor = 0.0f;
    horizon_factor *= horizon_factor;

    /* Sun color: warm at horizon, white at zenith */
    Vec3 sun_white = vec3_create(1.0f, 0.98f, 0.95f);
    Vec3 sun_warm = vec3_create(1.0f, 0.6f, 0.3f);
    Vec3 sun_color = vec3_lerp_f(sun_white, sun_warm, horizon_factor);

    /* Sun strength dims at night */
    float sun_strength = day_factor * lerp_f(1.0f, 0.7f, horizon_factor);

    /* Sky color */
    Vec3 sky_day = vec3_create(0.75f, 0.80f, 0.95f);
    Vec3 sky_sunset = vec3_create(0.9f, 0.55f, 0.35f);
    Vec3 sky_night = vec3_create(0.03f, 0.04f, 0.08f);
    Vec3 sky_color = vec3_lerp_f(sky_night, sky_day, day_factor);
    sky_color = vec3_lerp_f(sky_color, sky_sunset, horizon_factor * day_factor);

    /* Ambient */
    Vec3 sky_ambient_day = vec3_create(0.55f, 0.68f, 0.95f);
    Vec3 sky_ambient_night = vec3_create(0.05f, 0.06f, 0.12f);
    Vec3 sky_ambient = vec3_lerp_f(sky_ambient_night, sky_ambient_day, day_factor);

    Vec3 ground_ambient_day = vec3_create(0.38f, 0.32f, 0.28f);
    Vec3 ground_ambient_night = vec3_create(0.04f, 0.03f, 0.05f);
    Vec3 ground_ambient = vec3_lerp_f(ground_ambient_night, ground_ambient_day, day_factor);

    float ambient_strength = lerp_f(0.15f, 0.32f, day_factor);

    /* Fill light follows sun but offset */
    Vec3 fill_dir = vec3_create(
        sun_dir.x * 0.5f + 0.48f,
        sun_dir.y * 0.3f + 0.53f,
        sun_dir.z * 0.5f - 0.69f);
    len = sqrtf(fill_dir.x * fill_dir.x + fill_dir.y * fill_dir.y + fill_dir.z * fill_dir.z);
    if (len > 0.001f)
    {
        fill_dir.x /= len;
        fill_dir.y /= len;
        fill_dir.z /= len;
    }

    float fill_strength = lerp_f(0.08f, 0.25f, day_factor);
    Vec3 fill_color = vec3_lerp_f(vec3_create(0.3f, 0.35f, 0.5f), vec3_create(0.7f, 0.8f, 1.0f), day_factor);

    /* Back light */
    float back_strength = lerp_f(0.04f, 0.12f, day_factor);

    /* Exposure: slightly dim at night */
    float exposure = lerp_f(0.7f, 1.0f, day_factor);

    /* Emissive: brighter at night (glowing materials) */
    float emissive_mult = lerp_f(4.0f, 2.0f, day_factor);

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
    lighting->rim_strength = lerp_f(0.03f, 0.10f, day_factor);
}
