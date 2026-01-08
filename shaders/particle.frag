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

// Ray-sphere intersection
// Returns t (distance along ray) or -1 if no hit
float intersect_sphere(vec3 ray_origin, vec3 ray_dir, vec3 center, float radius) {
    vec3 oc = ray_origin - center;
    float a = dot(ray_dir, ray_dir);
    float b = 2.0 * dot(oc, ray_dir);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0.0) {
        return -1.0;
    }

    float t = (-b - sqrt(discriminant)) / (2.0 * a);
    if (t < 0.0) {
        t = (-b + sqrt(discriminant)) / (2.0 * a);
    }

    return t;
}

void main() {
    if (in_particle_index < 0) {
        discard;
    }

    ParticleGPU p = particles[in_particle_index];

    vec3 center = p.position_radius.xyz;
    float radius = p.position_radius.w;
    vec3 color = p.color_flags.rgb;

    vec3 ray_dir = normalize(in_world_pos - in_ray_origin);

    float t = intersect_sphere(in_ray_origin, ray_dir, center, radius);

    if (t < 0.0) {
        discard;
    }

    vec3 hit_point = in_ray_origin + ray_dir * t;
    vec3 normal = normalize(hit_point - center);

    float linear_depth = t;

    out_albedo = vec4(color, 1.0);
    out_normal = vec4(normal * 0.5 + 0.5, 1.0);
    out_material = vec4(0.8, 0.0, 0.0, 0.0);  // roughness=0.8, metallic=0, emissive=0
    out_linear_depth = linear_depth;

    float near = 0.1;
    float far = 100.0;
    gl_FragDepth = (far - near * far / linear_depth) / (far - near);
}
