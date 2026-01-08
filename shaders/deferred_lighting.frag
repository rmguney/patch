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
layout(SET_BINDING(0, 4)) uniform usampler3D shadow_volume_tex;
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
    int reserved[10];
} pc;

vec3 reconstruct_world_pos(vec2 uv, float depth) {
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 clip = vec4(ndc, 0.0, 1.0);
    vec4 view = pc.inv_projection * clip;
    view.xyz /= view.w;
    vec3 view_dir = normalize(view.xyz);
    vec3 world_dir = (pc.inv_view * vec4(view_dir, 0.0)).xyz;
    return pc.cam_pos + world_dir * depth;
}

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float linear_depth_to_ndc(float linear_depth, float near, float far) {
    return (far - near * far / linear_depth) / (far - near);
}

float traceShadowRayLOD(vec3 origin, vec3 dir, float max_dist) {
    ivec3 shadow_vol_size_0 = textureSize(shadow_volume_tex, 0);
    if (shadow_vol_size_0.x < 1) {
        return 1.0;
    }

    vec3 bounds_size = pc.bounds_max - pc.bounds_min;
    vec3 inv_bounds = 1.0 / bounds_size;

    float base_step = pc.voxel_size * 2.0;
    vec3 pos = origin + dir * pc.voxel_size;
    float t = pc.voxel_size;

    for (int lod = 2; lod >= 0; lod--)
    {
        ivec3 vol_size = textureSize(shadow_volume_tex, lod);
        float step_size = base_step * float(1 << lod);
        int max_steps = int(max_dist / step_size) + 1;
        max_steps = min(max_steps, 24);

        for (int i = 0; i < max_steps; i++)
        {
            vec3 uvw = (pos - pc.bounds_min) * inv_bounds;

            if (any(lessThan(uvw, vec3(0.0))) || any(greaterThanEqual(uvw, vec3(1.0)))) {
                return 1.0;
            }

            ivec3 texel = ivec3(uvw * vec3(vol_size));
            texel = clamp(texel, ivec3(0), vol_size - 1);

            uint occ = texelFetch(shadow_volume_tex, texel, lod).r;
            if (occ != 0u) {
                if (lod == 0) {
                    return 0.0;
                }
                break;
            }

            t += step_size;
            pos = origin + dir * t;

            if (t > max_dist) {
                return 1.0;
            }
        }
    }

    return 1.0;
}

float traceShadowRay(vec3 origin, vec3 dir, float max_dist) {
    return traceShadowRayLOD(origin, dir, max_dist);
}

void main() {
    vec4 albedo_sample = texture(gbuffer_albedo, in_uv);

    if (albedo_sample.a < 0.01) {
        vec3 sky_top = vec3(0.45, 0.65, 0.95);
        vec3 sky_bottom = vec3(0.78, 0.88, 0.98);
        out_color = vec4(mix(sky_bottom, sky_top, in_uv.y), 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    vec3 albedo = albedo_sample.rgb;
    vec3 normal = texture(gbuffer_normal, in_uv).rgb * 2.0 - 1.0;
    vec4 material = texture(gbuffer_material, in_uv);
    float linear_depth = texture(gbuffer_depth, in_uv).r;
    float near = 0.1;
    float far = 100.0;
    gl_FragDepth = linear_depth_to_ndc(max(linear_depth, near), near, far);

    float roughness = material.r;
    float metallic = material.g;
    float emissive = material.b;

    vec3 world_pos = reconstruct_world_pos(in_uv, linear_depth);
    vec3 N = normalize(normal);
    vec3 V = normalize(pc.cam_pos - world_pos);

    vec3 key_light_dir = normalize(vec3(-0.6, 0.9, 0.35));
    vec3 key_color = vec3(1.05, 1.0, 0.96);
    float key_strength = 1.25;

    float shadow = 1.0;
    if (pc.rt_quality > 0) {
        vec3 shadow_origin = world_pos + N * pc.voxel_size * 0.5;

        vec2 noise_uv = gl_FragCoord.xy / 128.0;
        float noise = texture(blue_noise_tex, noise_uv).r;
        noise = fract(noise + float(pc.frame_count) * 0.6180339887);

        float penumbra = 0.01 + 0.01 * float(pc.rt_quality);
        float angle = noise * 6.28318;
        float radius = noise * penumbra;
        vec3 tangent = normalize(cross(key_light_dir, vec3(0.0, 1.0, 0.01)));
        vec3 bitangent = cross(key_light_dir, tangent);
        vec3 jitter = tangent * cos(angle) * radius + bitangent * sin(angle) * radius;
        vec3 jittered_light = normalize(key_light_dir + jitter);

        shadow = traceShadowRay(shadow_origin, jittered_light, 30.0);
        shadow = mix(0.35, 1.0, shadow);
    }

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

    float exposure = 1.0;
    color *= exposure;

    color = aces_tonemap(color);
    color = pow(color, vec3(1.0 / 2.2));

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
