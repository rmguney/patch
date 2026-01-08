#ifndef PATCH_CORE_LIGHTING_H
#define PATCH_CORE_LIGHTING_H

#include "types.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Vec3 direction;
    Vec3 color;
    float intensity;
} DirectionalLight;

typedef struct {
    Vec3 position;
    Vec3 color;
    float intensity;
    float radius;
    float falloff;
} PointLight;

typedef struct {
    DirectionalLight sun;
    DirectionalLight fill;
    Vec3 sky_color;
    Vec3 ground_color;
    Vec3 bounce_color;
    float ambient_intensity;
    float ao_strength;
    float ao_radius;
} LightingEnvironment;

static inline LightingEnvironment lighting_environment_default(void) {
    LightingEnvironment env;
    
    env.sun.direction = vec3_normalize(vec3_create(0.5f, 0.8f, 0.4f));
    env.sun.color = vec3_create(1.0f, 0.95f, 0.9f);
    env.sun.intensity = 2.5f;
    
    env.fill.direction = vec3_normalize(vec3_create(-0.5f, 0.3f, -0.6f));
    env.fill.color = vec3_create(0.7f, 0.8f, 1.0f);
    env.fill.intensity = 0.6f;
    
    env.sky_color = vec3_create(0.5f, 0.7f, 1.0f);
    env.ground_color = vec3_create(0.4f, 0.35f, 0.3f);
    env.bounce_color = vec3_create(0.95f, 0.88f, 0.82f);
    
    env.ambient_intensity = 0.25f;
    env.ao_strength = 0.3f;
    env.ao_radius = 1.5f;
    
    return env;
}

static inline float lighting_calculate_ao(float height_above_ground, float ao_radius, float ao_strength) {
    float ao = 1.0f;
    if (height_above_ground < ao_radius) {
        float factor = height_above_ground / ao_radius;
        ao = factor * ao_strength + (1.0f - ao_strength);
    }
    return ao;
}

static inline Vec3 lighting_hemisphere_ambient(Vec3 normal, Vec3 sky_color, Vec3 ground_color, float intensity) {
    float blend = normal.y * 0.5f + 0.5f;
    Vec3 ambient;
    ambient.x = (ground_color.x * (1.0f - blend) + sky_color.x * blend) * intensity;
    ambient.y = (ground_color.y * (1.0f - blend) + sky_color.y * blend) * intensity;
    ambient.z = (ground_color.z * (1.0f - blend) + sky_color.z * blend) * intensity;
    return ambient;
}

static inline float lighting_wrap_diffuse(Vec3 normal, Vec3 light_dir, float wrap_amount) {
    float ndotl = vec3_dot(normal, light_dir);
    float wrap = (ndotl + wrap_amount) / (1.0f + wrap_amount);
    return wrap > 0.0f ? wrap : 0.0f;
}

#ifdef __cplusplus
}
#endif

#endif
