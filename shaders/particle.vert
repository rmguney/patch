#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

struct ParticleGPU {
    vec4 position_radius;  // xyz = position, w = radius
    vec4 color_flags;      // rgb = color, a = flags (1.0 = active)
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

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec3 out_ray_origin;
layout(location = 2) out flat int out_particle_index;

const vec3 CUBE_VERTICES[36] = vec3[36](
    vec3(-1, -1, -1), vec3(-1, -1,  1), vec3(-1,  1,  1),
    vec3(-1, -1, -1), vec3(-1,  1,  1), vec3(-1,  1, -1),
    vec3( 1, -1, -1), vec3( 1,  1,  1), vec3( 1, -1,  1),
    vec3( 1, -1, -1), vec3( 1,  1, -1), vec3( 1,  1,  1),
    vec3(-1, -1, -1), vec3( 1, -1, -1), vec3( 1, -1,  1),
    vec3(-1, -1, -1), vec3( 1, -1,  1), vec3(-1, -1,  1),
    vec3(-1,  1, -1), vec3( 1,  1,  1), vec3( 1,  1, -1),
    vec3(-1,  1, -1), vec3(-1,  1,  1), vec3( 1,  1,  1),
    vec3(-1, -1, -1), vec3(-1,  1, -1), vec3( 1,  1, -1),
    vec3(-1, -1, -1), vec3( 1,  1, -1), vec3( 1, -1, -1),
    vec3(-1, -1,  1), vec3( 1,  1,  1), vec3(-1,  1,  1),
    vec3(-1, -1,  1), vec3( 1, -1,  1), vec3( 1,  1,  1)
);

void main() {
    int vertex_id = gl_VertexIndex % 36;
    int particle_idx = gl_InstanceIndex;

    if (particle_idx >= pc.particle_count) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        out_world_pos = vec3(0.0);
        out_ray_origin = vec3(0.0);
        out_particle_index = -1;
        return;
    }

    ParticleGPU p = particles[particle_idx];

    if (p.color_flags.a < 0.5) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        out_world_pos = vec3(0.0);
        out_ray_origin = vec3(0.0);
        out_particle_index = -1;
        return;
    }

    vec3 center = p.position_radius.xyz;
    float radius = p.position_radius.w;
    float expand = 1.1;

    vec3 local_pos = CUBE_VERTICES[vertex_id] * radius * expand;
    vec3 world_pos = center + local_pos;

    gl_Position = pc.view_proj * vec4(world_pos, 1.0);
    out_world_pos = world_pos;
    out_ray_origin = pc.camera_pos;
    out_particle_index = particle_idx;
}
