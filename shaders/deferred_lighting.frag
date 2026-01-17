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
layout(SET_BINDING(0, 7)) uniform sampler2D ao_buffer;
layout(SET_BINDING(0, 8)) uniform sampler2D reflection_buffer;

/* GI radiance cascade samplers */
layout(SET_BINDING(0, 9)) uniform sampler3D gi_cascade_0;
layout(SET_BINDING(0, 10)) uniform sampler3D gi_cascade_1;
layout(SET_BINDING(0, 11)) uniform sampler3D gi_cascade_2;
layout(SET_BINDING(0, 12)) uniform sampler3D gi_cascade_3;

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
    int _pad0;
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
    int reflection_quality;
} pc;

#include "include/gbuffer_sample.glsl"

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/* Extract GI quality from history_valid (packed in bits 8-15) */
int get_gi_quality() {
    return (pc.history_valid >> 8) & 0xFF;
}

/* Sample indirect radiance from GI cascades */
vec3 sample_gi_radiance(vec3 world_pos, vec3 normal) {
    int gi_quality = get_gi_quality();
    if (gi_quality == 0) {
        return vec3(0.0);
    }

    vec3 bounds_size = pc.bounds_max - pc.bounds_min;
    vec3 uvw = (world_pos - pc.bounds_min) / bounds_size;

    /* Clamp to valid cascade range */
    uvw = clamp(uvw, vec3(0.001), vec3(0.999));

    /* Sample multiple cascade levels and blend based on distance from camera */
    float dist_to_cam = length(world_pos - pc.cam_pos);

    /* Cascade level selection based on distance:
       Level 0: near (highest detail), Level 3: far (lowest detail) */
    float level_f = clamp(dist_to_cam / 20.0, 0.0, 3.0);
    int level_low = int(floor(level_f));
    int level_high = min(level_low + 1, 3);
    float blend = fract(level_f);

    vec3 radiance_low, radiance_high;

    /* Sample cascades (can't use arrays with samplers, so explicit switch) */
    if (level_low == 0) {
        radiance_low = texture(gi_cascade_0, uvw).rgb;
    } else if (level_low == 1) {
        radiance_low = texture(gi_cascade_1, uvw).rgb;
    } else if (level_low == 2) {
        radiance_low = texture(gi_cascade_2, uvw).rgb;
    } else {
        radiance_low = texture(gi_cascade_3, uvw).rgb;
    }

    if (level_high == 0) {
        radiance_high = texture(gi_cascade_0, uvw).rgb;
    } else if (level_high == 1) {
        radiance_high = texture(gi_cascade_1, uvw).rgb;
    } else if (level_high == 2) {
        radiance_high = texture(gi_cascade_2, uvw).rgb;
    } else {
        radiance_high = texture(gi_cascade_3, uvw).rgb;
    }

    vec3 radiance = mix(radiance_low, radiance_high, blend);

    /* Apply hemisphere weighting based on normal */
    float hemisphere_weight = max(dot(normal, vec3(0.0, 1.0, 0.0)), 0.0) * 0.5 + 0.5;
    radiance *= hemisphere_weight;

    /* Quality-based intensity scaling */
    float gi_strength = float(gi_quality) * 0.33; /* 1=0.33, 2=0.66, 3=1.0 */

    return radiance * gi_strength;
}

void main() {
    GBufferSample g = sample_gbuffer(
        in_uv,
        gbuffer_albedo, gbuffer_normal, gbuffer_material, gbuffer_depth,
        pc.inv_view, pc.inv_projection, pc.cam_pos,
        pc.is_orthographic != 0
    );

    if (g.is_sky) {
        out_color = vec4(0.75, 0.80, 0.95, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    gl_FragDepth = camera_linear_depth_to_ndc(max(g.linear_depth, pc.near_plane), pc.near_plane, pc.far_plane);

    vec3 N = normalize(g.normal);
    vec3 V = normalize(pc.cam_pos - g.world_pos);

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

    /* DEBUG: Show reflection buffer (RGB=reflection color, A=blend weight) */
    if (pc.debug_mode == 14) {
        vec4 refl = texture(reflection_buffer, in_uv);
        out_color = vec4(refl.rgb, 1.0);
        return;
    }

    /* DEBUG: Show GI radiance contribution */
    if (pc.debug_mode == 15) {
        vec3 gi = sample_gi_radiance(g.world_pos, N);
        out_color = vec4(gi, 1.0);
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
    diffuse += g.albedo * key_color * wrap_key * key_strength * shadow;
    diffuse += g.albedo * fill_color * fill_dot * fill_strength;
    diffuse += g.albedo * back_color * back_dot * back_strength;

    /* Sample AO from compute pass (1 = unoccluded, 0 = fully occluded) */
    float ao = texture(ao_buffer, in_uv).r;

    float ambient = 0.4;
    vec3 sky_ambient = vec3(0.62, 0.74, 0.98) * (N.y * 0.5 + 0.5);
    vec3 ground_ambient = vec3(0.42, 0.36, 0.32) * (0.5 - N.y * 0.5);
    vec3 ambient_light = (sky_ambient + ground_ambient) * ambient * ao;

    /* Add GI indirect radiance to ambient */
    vec3 gi_radiance = sample_gi_radiance(g.world_pos, N);
    ambient_light += gi_radiance * ao;

    vec3 H = normalize(key_light_dir + V);
    float spec_power = mix(128.0, 4.0, g.roughness * g.roughness);
    float spec_strength = mix(0.4, 0.02, g.roughness);
    float spec = pow(max(dot(N, H), 0.0), spec_power) * spec_strength;
    vec3 specular = vec3(spec) * key_color * key_dot * shadow;

    vec3 H_fill = normalize(fill_light_dir + V);
    float spec_fill = pow(max(dot(N, H_fill), 0.0), spec_power * 0.5) * spec_strength * 0.3;
    specular += vec3(spec_fill) * fill_color * fill_dot;

    float floor_y = pc.bounds_min.y;
    float ground_dist = g.world_pos.y - floor_y;
    float ground_ao = smoothstep(0.0, 1.5, ground_dist) * 0.3 + 0.7;

    vec3 color = (g.albedo * ambient_light + diffuse + specular) * ground_ao;

    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 4.0) * 0.08;
    color += rim * vec3(0.7, 0.8, 1.0);

    vec3 emissive_color = g.albedo * g.emissive * 2.0;
    color += emissive_color;

    /* Sample reflection buffer and blend */
    vec4 reflection = texture(reflection_buffer, in_uv);
    if (reflection.a > 0.001) {
        /* For metals: reflection contributes more, replacing some diffuse
           For dielectrics: additive reflection with Fresnel weight */
        float metallic_blend = 0.5 + 0.5 * g.metallic;
        float blend = reflection.a * metallic_blend;
        /* Metals get reflection mixed with ambient, dielectrics get additive */
        color = mix(color, reflection.rgb + ambient_light, blend * g.metallic)
              + reflection.rgb * blend * (1.0 - g.metallic);
    }

    float exposure = 1.0;
    color *= exposure;

    color = aces_tonemap(color);
    color = pow(color, vec3(1.0 / 2.2));

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
