#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

#include "include/camera.glsl"
#include "include/hdda_core.glsl"

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_ray_origin;
layout(location = 2) in flat int in_particle_index;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_linear_depth;
layout(location = 4) out vec2 out_motion_vector;

struct ParticleGPU {
    vec4 position_radius;  // xyz = position, w = radius
    vec4 color_flags;      // rgb = color, a = flags
};

layout(std430, SET_BINDING(0, 0)) readonly buffer ParticleBuffer {
    ParticleGPU particles[];
};

layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float pad1;
    int particle_count;
    float near_plane;
    float far_plane;
    int pad2;
} pc;

// Get box face normal from hit point
vec3 box_normal(vec3 hit_point, vec3 box_min, vec3 box_max) {
    vec3 center = (box_min + box_max) * 0.5;
    vec3 half_size = (box_max - box_min) * 0.5;
    vec3 rel = hit_point - center;
    vec3 abs_rel = abs(rel) / half_size;
    float max_comp = max(max(abs_rel.x, abs_rel.y), abs_rel.z);

    if (abs_rel.x >= max_comp - 0.001) return vec3(sign(rel.x), 0.0, 0.0);
    if (abs_rel.y >= max_comp - 0.001) return vec3(0.0, sign(rel.y), 0.0);
    return vec3(0.0, 0.0, sign(rel.z));
}

void main() {
    if (in_particle_index < 0) {
        discard;
    }

    ParticleGPU p = particles[in_particle_index];

    vec3 center = p.position_radius.xyz;
    float half_size = p.position_radius.w;
    vec3 color = p.color_flags.rgb;

    vec3 box_min = center - vec3(half_size);
    vec3 box_max = center + vec3(half_size);

    vec3 ray_dir = normalize(in_world_pos - in_ray_origin);

    vec2 t_hit = hdda_intersect_aabb(in_ray_origin, ray_dir, box_min, box_max);

    if (t_hit.x > t_hit.y || t_hit.y < 0.0) {
        discard;
    }

    /* Use entry point if outside box, exit point if inside (camera inside particle) */
    float t = t_hit.x > 0.0 ? t_hit.x : t_hit.y;
    t = max(t, 0.001);
    vec3 hit_point = in_ray_origin + ray_dir * t;

    /* Normal: use entry face if outside, exit face if inside */
    vec3 normal = box_normal(hit_point, box_min, box_max);
    if (t_hit.x <= 0.0) {
        normal = -normal; /* Flip normal when viewing from inside */
    }

    float linear_depth = t;

    out_albedo = vec4(color, 1.0);
    out_normal = vec4(normal * 0.5 + 0.5, 1.0);
    out_material = vec4(0.8, 0.0, 0.0, 0.0);  // roughness=0.8, metallic=0, emissive=0
    out_linear_depth = linear_depth;
    out_motion_vector = vec2(0.0);

    gl_FragDepth = clamp(camera_linear_depth_to_ndc(linear_depth, pc.near_plane, pc.far_plane), 0.0, 1.0);
}
