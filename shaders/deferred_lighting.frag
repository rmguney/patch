#version 450

/*
 * deferred_lighting.frag - shades the G-buffer (sun with soft shadows, fill / back lights,
 * hemispheric ambient with AO, specular, environment reflection, rim), then ACES tone
 * mapping and the display encode.
 */

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D gbuffer_albedo;
layout(set = 0, binding = 1) uniform sampler2D gbuffer_normal;
layout(set = 0, binding = 2) uniform usampler2D gbuffer_material;
layout(set = 0, binding = 3) uniform sampler2D gbuffer_depth;
layout(set = 0, binding = 4) uniform sampler2D shadow_buffer;
layout(set = 0, binding = 5) uniform sampler2D ao_buffer;

/* The palette: the water a pixel was seen through absorbs by its material's colour */
layout(std140, set = 0, binding = 6) uniform MaterialPalette {
    vec4 materials[512];
};

#include "include/materials.glsl"

#define LIGHTING_SET 1
#include "include/scene_lighting.glsl"

#include "include/voxel_push_constants.glsl"

#include "include/gbuffer_sample.glsl"
#include "include/debug_views.glsl"

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/* Raw G-buffer channels for the debug views; false when mode is not one of them */
bool gbuffer_debug_view(GBufferSample g, int mode, out vec3 color) {
    switch (mode) {
    case DEBUG_VIEW_NORMALS:     color = normalize(g.normal) * 0.5 + 0.5; return true;
    case DEBUG_VIEW_ALBEDO:      color = g.albedo; return true;
    case DEBUG_VIEW_DEPTH:       color = vec3(1.0 - clamp(g.linear_depth / pc.far_plane, 0.0, 1.0)); return true;
    case DEBUG_VIEW_ROUGHNESS:   color = vec3(g.roughness); return true;
    case DEBUG_VIEW_METALLIC:    color = vec3(g.metallic); return true;
    case DEBUG_VIEW_EMISSIVE:    color = vec3(g.emissive); return true;
    case DEBUG_VIEW_WATER_DEPTH: color = vec3(0.2, 0.5, 1.0) * clamp(g.water_depth * 0.25, 0.0, 1.0); return true;
    default: return false;
    }
}

void main() {
    GBufferSample g = sample_gbuffer(ivec2(gl_FragCoord.xy), gbuffer_albedo, gbuffer_normal, gbuffer_material, gbuffer_depth);

    if (g.is_sky) {
        out_color = vec4(lighting.sky_color_params.rgb, 1.0);
        return;
    }

    vec3 debug_color;
    if (gbuffer_debug_view(g, pc.debug_mode, debug_color)) {
        out_color = vec4(debug_color, 1.0);
        return;
    }

    vec3 N = normalize(g.normal);
    /* View direction: from the surface to the camera */
    vec3 V = normalize(pc.camera_pos - g.world_pos);

    /* DEBUG: Visualize world position (should be stable when camera moves) */
    if (pc.debug_mode == DEBUG_VIEW_WORLD_POS) {
        vec3 wp_color = fract(g.world_pos * 0.1);
        out_color = vec4(wp_color, 1.0);
        return;
    }

    /* DEBUG: Visualize shadow UVW (should map 0-1 across terrain bounds) */
    if (pc.debug_mode == DEBUG_VIEW_SHADOW_UVW) {
        vec3 bounds_size = pc.bounds_max - pc.bounds_min;
        vec3 uvw = (g.world_pos - pc.bounds_min) / bounds_size;
        out_color = vec4(uvw, 1.0);
        return;
    }

    /* DEBUG: Show shadow value only (white=lit, black=shadow) */
    if (pc.debug_mode == DEBUG_VIEW_SHADOW) {
        float s = texture(shadow_buffer, in_uv).r;
        out_color = vec4(vec3(s), 1.0);
        return;
    }

    /* DEBUG: Show AO value only (white=unoccluded, black=occluded) */
    if (pc.debug_mode == DEBUG_VIEW_AO) {
        float ao = texture(ao_buffer, in_uv).r;
        out_color = vec4(vec3(ao), 1.0);
        return;
    }

    vec3 key_light_dir = lighting.sun_dir.xyz;
    vec3 key_color = lighting.sun_color.rgb;
    float key_strength = lighting.sun_color.w;

    /* Sample precomputed shadow from compute pass (white with shadows off) */
    float shadow = texture(shadow_buffer, in_uv).r;

    vec3 fill_light_dir = lighting.fill_dir.xyz;
    vec3 fill_color = lighting.fill_color.rgb;
    float fill_strength = lighting.fill_dir.w;

    vec3 back_light_dir = lighting.back_dir.xyz;
    vec3 back_color = lighting.back_color.rgb;
    float back_strength = lighting.fill_color.w;

    float key_dot = max(dot(N, key_light_dir), 0.0);
    float fill_dot = max(dot(N, fill_light_dir), 0.0);
    float back_dot = max(dot(N, back_light_dir), 0.0);

    float wrap_bias = lighting.sun_dir.w;
    float wrap_key = (dot(N, key_light_dir) + wrap_bias) / (1.0 + wrap_bias);
    wrap_key = max(wrap_key, 0.0);

    vec3 diffuse = vec3(0.0);
    diffuse += g.albedo * key_color * wrap_key * key_strength * shadow;
    diffuse += g.albedo * fill_color * fill_dot * fill_strength;
    diffuse += g.albedo * back_color * back_dot * back_strength;

    /* Sample AO from compute pass (1 = unoccluded, 0 = fully occluded; white with AO off) */
    float ao = texture(ao_buffer, in_uv).r;

    float ambient = lighting.back_color.w;
    vec3 sky_ambient = lighting.sky_ambient.rgb * (N.y * 0.5 + 0.5);
    vec3 ground_ambient = lighting.ground_ambient.rgb * (0.5 - N.y * 0.5);
    vec3 ambient_light = (sky_ambient + ground_ambient) * ambient * ao;

    vec3 H = normalize(key_light_dir + V);
    float spec_power = mix(128.0, 4.0, g.roughness * g.roughness);
    float spec_strength = mix(0.4, 0.02, g.roughness);
    float spec = pow(max(dot(N, H), 0.0), spec_power) * spec_strength;
    vec3 specular = vec3(spec) * key_color * key_dot * shadow;

    vec3 H_fill = normalize(fill_light_dir + V);
    float spec_fill = pow(max(dot(N, H_fill), 0.0), spec_power * 0.5) * spec_strength * 0.3;
    specular += vec3(spec_fill) * fill_color * fill_dot;

    /* Metallic specular tinting: metals reflect their own albedo color */
    vec3 spec_tint = mix(vec3(1.0), g.albedo, g.metallic);
    specular *= spec_tint;

    float NdotV = max(dot(N, V), 0.0);
    vec3 R = reflect(-V, N);
    vec3 env_color = mix(vec3(0.30, 0.22, 0.18), vec3(0.45, 0.65, 1.05), R.y * 0.5 + 0.5);
    float smoothness = 1.0 - g.roughness;
    float f0 = mix(0.04, 1.0, g.metallic);
    float fresnel = pow(1.0 - NdotV, 4.0);
    float F = f0 + (1.0 - f0) * fresnel;
    float env_total = F * smoothness * mix(0.7, 0.9, g.metallic);
    vec3 env_spec = env_color * env_total * ao;

    float floor_y = pc.bounds_min.y;
    float ground_dist = g.world_pos.y - floor_y;
    float ground_ao = smoothstep(0.0, 1.0, ground_dist) * 0.5 + 0.5;

    float diffuse_atten = 1.0 - env_total;
    vec3 color = (g.albedo * ambient_light * diffuse_atten + diffuse * diffuse_atten + specular + env_spec) * ground_ao;

    float rim = pow(1.0 - NdotV, 3.0) * lighting.back_dir.w;
    vec3 rim_tint = mix(env_color * g.albedo, env_color, g.metallic);
    color += rim * rim_tint * smoothness;

    /* DEBUG: Specular + environment tint isolation */
    if (pc.debug_mode == DEBUG_VIEW_SPECULAR_ENV) {
        vec3 rim_color = rim * env_color * smoothness;
        vec3 debug_spec = (specular + env_spec + rim_color) * ground_ao;
        debug_spec = aces_tonemap(debug_spec * 3.0);
        debug_spec = pow(debug_spec, vec3(1.0 / 2.2));
        out_color = vec4(clamp(debug_spec, 0.0, 1.0), 1.0);
        return;
    }

    vec3 emissive_color = g.albedo * g.emissive * lighting.ground_ambient.w;
    color += emissive_color;

    /* Water absorption via Beer's law, by the colour of the water's material */
    if (g.water_depth > 0.0) {
        vec3 transmittance = exp(-water_absorption(g.water_material) * g.water_depth);
        color *= transmittance;
        float water_fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);
        color += lighting.sky_color_params.rgb * water_fresnel * 0.15;
    }

    float exposure = lighting.sky_ambient.w;
    color *= exposure;

    color = aces_tonemap(color);
    /* The only display encode in the pipeline: the swapchain and lit_color are UNORM
     * (see Swapchain::create_swapchain); TAA and denoise work on these encoded values. */
    color = pow(color, vec3(1.0 / 2.2));

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
