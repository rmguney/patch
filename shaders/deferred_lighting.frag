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
layout(SET_BINDING(0, 5)) uniform sampler2D shadow_buffer;
layout(SET_BINDING(0, 6)) uniform sampler2D blue_noise_tex;

layout(push_constant) uniform Constants {
    mat4 inv_view;
    mat4 inv_projection;
    vec3 bounds_min;
    float voxel_size;
    vec3 bounds_max;
    float chunk_size;
    vec3 cam_pos;
    float pad1;
    ivec3 grid_size;
    int total_chunks;
    ivec3 chunks_dim;
    int frame_count;
    int rt_quality;
    int debug_mode;
    int is_orthographic;
    int max_steps;
    float near_plane;
    float far_plane;
    int reserved[6];
} pc;

#include "include/camera.glsl"

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec4 albedo_sample = texture(gbuffer_albedo, in_uv);

    if (albedo_sample.a < 0.01) {
        out_color = vec4(0.75, 0.80, 0.95, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    vec3 albedo = albedo_sample.rgb;
    vec3 normal = texture(gbuffer_normal, in_uv).rgb * 2.0 - 1.0;
    vec4 material = texture(gbuffer_material, in_uv);
    float linear_depth = texture(gbuffer_depth, in_uv).r;
    gl_FragDepth = camera_linear_depth_to_ndc(max(linear_depth, pc.near_plane), pc.near_plane, pc.far_plane);

    float roughness = material.r;
    float metallic = material.g;
    float emissive = material.b;

    vec3 world_pos = camera_reconstruct_world_pos(
        in_uv, linear_depth,
        pc.inv_view, pc.inv_projection, pc.cam_pos,
        pc.is_orthographic != 0
    );
    vec3 N = normalize(normal);
    vec3 V = normalize(pc.cam_pos - world_pos);

    /* DEBUG: Visualize world position (should be stable when camera moves) */
    if (pc.debug_mode == 10) {
        vec3 wp_color = fract(world_pos * 0.1);
        out_color = vec4(wp_color, 1.0);
        return;
    }

    /* DEBUG: Visualize shadow UVW (should map 0-1 across terrain bounds) */
    if (pc.debug_mode == 11) {
        vec3 bounds_size = pc.bounds_max - pc.bounds_min;
        vec3 uvw = (world_pos - pc.bounds_min) / bounds_size;
        out_color = vec4(uvw, 1.0);
        return;
    }

    /* DEBUG: Show shadow value only (white=lit, black=shadow) */
    if (pc.debug_mode == 12) {
        float s = texture(shadow_buffer, in_uv).r;
        out_color = vec4(vec3(s), 1.0);
        return;
    }

    vec3 key_light_dir = normalize(vec3(-0.6, 0.9, 0.35));
    vec3 key_color = vec3(1.0, 0.98, 0.95);
    float key_strength = 1.0;

    /* Sample precomputed shadow from compute pass */
    float shadow = texture(shadow_buffer, in_uv).r;

    vec3 fill_light_dir = normalize(vec3(0.45, 0.5, -0.65));
    vec3 fill_color = vec3(0.7, 0.8, 1.0);
    float fill_strength = 0.45;

    vec3 back_light_dir = normalize(vec3(-0.35, 0.28, 0.9));
    vec3 back_color = vec3(0.95, 0.9, 0.85);
    float back_strength = 0.25;

    float key_dot = max(dot(N, key_light_dir), 0.0);
    float fill_dot = max(dot(N, fill_light_dir), 0.0);
    float back_dot = max(dot(N, back_light_dir), 0.0);

    float wrap_key = (dot(N, key_light_dir) + 0.4) / 1.4;
    wrap_key = max(wrap_key, 0.0);

    vec3 diffuse = vec3(0.0);
    diffuse += albedo * key_color * wrap_key * key_strength * shadow;
    diffuse += albedo * fill_color * fill_dot * fill_strength;
    diffuse += albedo * back_color * back_dot * back_strength;

    float ambient = 0.4;
    vec3 sky_ambient = vec3(0.62, 0.74, 0.98) * (N.y * 0.5 + 0.5);
    vec3 ground_ambient = vec3(0.42, 0.36, 0.32) * (0.5 - N.y * 0.5);
    vec3 ambient_light = (sky_ambient + ground_ambient) * ambient;

    vec3 H = normalize(key_light_dir + V);
    float spec_power = mix(128.0, 4.0, roughness * roughness);
    float spec_strength = mix(0.4, 0.02, roughness);
    float spec = pow(max(dot(N, H), 0.0), spec_power) * spec_strength;
    vec3 specular = vec3(spec) * key_color * key_dot * shadow;

    vec3 H_fill = normalize(fill_light_dir + V);
    float spec_fill = pow(max(dot(N, H_fill), 0.0), spec_power * 0.5) * spec_strength * 0.3;
    specular += vec3(spec_fill) * fill_color * fill_dot;

    float floor_y = pc.bounds_min.y;
    float ground_dist = world_pos.y - floor_y;
    float ground_ao = smoothstep(0.0, 1.5, ground_dist) * 0.3 + 0.7;

    vec3 color = (albedo * ambient_light + diffuse + specular) * ground_ao;

    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 4.0) * 0.08;
    color += rim * vec3(0.7, 0.8, 1.0);

    vec3 emissive_color = albedo * emissive * 2.0;
    color += emissive_color;

    float exposure = 0.7;
    color *= exposure;

    color = aces_tonemap(color);
    color = pow(color, vec3(1.0 / 2.2));

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
