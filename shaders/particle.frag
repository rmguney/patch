#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_ray_origin;
layout(location = 2) in flat int in_particle_index;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_linear_depth;

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
    int pad2[3];
} pc;

// Ray-box intersection for axis-aligned cube
// Returns vec2(t_near, t_far) or vec2(-1) if no hit
vec2 intersect_box(vec3 ray_origin, vec3 ray_dir, vec3 box_min, vec3 box_max) {
    vec3 inv_dir = 1.0 / ray_dir;
    vec3 t0 = (box_min - ray_origin) * inv_dir;
    vec3 t1 = (box_max - ray_origin) * inv_dir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float t_near = max(max(tmin.x, tmin.y), tmin.z);
    float t_far = min(min(tmax.x, tmax.y), tmax.z);
    if (t_near > t_far || t_far < 0.0) {
        return vec2(-1.0);
    }
    return vec2(t_near, t_far);
}

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

    vec2 t_hit = intersect_box(in_ray_origin, ray_dir, box_min, box_max);

    if (t_hit.x < 0.0) {
        discard;
    }

    float t = max(t_hit.x, 0.001);
    vec3 hit_point = in_ray_origin + ray_dir * t;
    vec3 normal = box_normal(hit_point, box_min, box_max);

    float linear_depth = t;

    out_albedo = vec4(color, 1.0);
    out_normal = vec4(normal * 0.5 + 0.5, 1.0);
    out_material = vec4(0.8, 0.0, 0.0, 0.0);  // roughness=0.8, metallic=0, emissive=0
    out_linear_depth = linear_depth;

    float near = 0.1;
    float far = 100.0;
    gl_FragDepth = (far - near * far / linear_depth) / (far - near);
}
