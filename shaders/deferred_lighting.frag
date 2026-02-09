#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(SET_BINDING(0, 0)) uniform sampler2D gbuffer_albedo;
layout(SET_BINDING(0, 1)) uniform sampler2D gbuffer_normal;
layout(SET_BINDING(0, 2)) uniform sampler2D gbuffer_material;
layout(SET_BINDING(0, 3)) uniform sampler2D gbuffer_depth;
layout(SET_BINDING(0, 4)) uniform sampler2D gbuffer_world_pos;
layout(SET_BINDING(0, 5)) uniform sampler2D shadow_buffer;
layout(SET_BINDING(0, 6)) uniform sampler2D blue_noise_tex;
layout(SET_BINDING(0, 7)) uniform sampler2D ao_buffer;

#define LIGHTING_SET 1
#include "include/scene_lighting.glsl"

layout(push_constant) uniform Constants {
    mat4 inv_view;
    mat4 inv_projection;
    vec3 bounds_min;
    float voxel_size;
    vec3 bounds_max;
    float chunk_size;
    vec3 cam_pos;
    int history_valid;
    ivec3 grid_size;
    int total_chunks;
    ivec3 chunks_dim;
    int frame_count;
    int object_shadow_quality;
    int debug_mode;
    int is_orthographic;
    int max_steps;
    float near_plane;
    float far_plane;
    int object_count;
    int shadow_quality;
    int shadow_contact;
    int ao_quality;
    int lod_quality;
    int taa_quality;
} pc;

#include "include/camera.glsl"
#include "include/gbuffer_sample.glsl"

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    GBufferSample g = sample_gbuffer(
        in_uv,
        gbuffer_albedo, gbuffer_normal, gbuffer_material, gbuffer_depth,
        gbuffer_world_pos
    );

    if (g.is_sky) {
        out_color = vec4(lighting.sky_color_params.rgb, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    bool is_ortho = pc.is_orthographic != 0;
    gl_FragDepth = camera_linear_depth_to_ndc_ortho(max(g.linear_depth, pc.near_plane), pc.near_plane, pc.far_plane, is_ortho);

    vec3 N = normalize(g.normal);
    /* View direction: for perspective, from surface to camera; for ortho, use negative camera forward */
    vec3 V = is_ortho ? -camera_get_forward(pc.inv_view) : normalize(pc.cam_pos - g.world_pos);

    /* DEBUG: Visualize world position (should be stable when camera moves) */
    if (pc.debug_mode == 10) {
        vec3 wp_color = fract(g.world_pos * 0.1);
        out_color = vec4(wp_color, 1.0);
        return;
    }

    /* DEBUG: Visualize shadow UVW (should map 0-1 across terrain bounds) */
    if (pc.debug_mode == 11) {
        vec3 bounds_size = pc.bounds_max - pc.bounds_min;
        vec3 uvw = (g.world_pos - pc.bounds_min) / bounds_size;
        out_color = vec4(uvw, 1.0);
        return;
    }

    /* DEBUG: Show shadow value only (white=lit, black=shadow) */
    if (pc.debug_mode == 12) {
        float s = texture(shadow_buffer, in_uv).r;
        out_color = vec4(vec3(s), 1.0);
        return;
    }

    /* DEBUG: Show AO value only (white=unoccluded, black=occluded) */
    if (pc.debug_mode == 13) {
        float ao = texture(ao_buffer, in_uv).r;
        out_color = vec4(vec3(ao), 1.0);
        return;
    }

    vec3 key_light_dir = lighting.sun_dir.xyz;
    vec3 key_color = lighting.sun_color.rgb;
    float key_strength = lighting.sun_color.w;

    /* Sample precomputed shadow from compute pass */
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

    /* Sample AO from compute pass (1 = unoccluded, 0 = fully occluded) */
    float ao = (pc.ao_quality >= 1) ? texture(ao_buffer, in_uv).r : 1.0;

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
    if (pc.debug_mode == 16) {
        vec3 rim_color = rim * env_color * smoothness;
        vec3 debug_spec = (specular + env_spec + rim_color) * ground_ao;
        debug_spec = aces_tonemap(debug_spec * 3.0);
        debug_spec = pow(debug_spec, vec3(1.0 / 2.2));
        out_color = vec4(clamp(debug_spec, 0.0, 1.0), 1.0);
        return;
    }

    vec3 emissive_color = g.albedo * g.emissive * lighting.ground_ambient.w;
    color += emissive_color;

    /* Water absorption via Beer's law */
    if (g.water_depth > 0.0) {
        vec3 absorption = vec3(0.2, 0.05, 0.01);
        vec3 transmittance = exp(-absorption * g.water_depth);
        color *= transmittance;
        float water_fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);
        color += lighting.sky_color_params.rgb * water_fresnel * 0.15;
    }

    float exposure = lighting.sky_ambient.w;
    color *= exposure;

    color = aces_tonemap(color);
    color = pow(color, vec3(1.0 / 2.2));

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
