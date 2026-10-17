#version 450

/* particle.vert - proxy cubes for the raymarched particles (debris and weather). The CPU
 * draws the cubes the camera is outside of with their front faces at their depth, and those it
 * is inside of, or so close to that the near plane would clip their front faces, with their
 * back faces on the near plane (pc.camera_inside), which keeps the fragment shader's
 * conservative depth valid. */

/* Mirror of ParticleGPU (engine/render/raster_gpu_types.h) */
struct ParticleGPU {
    vec4 position_radius;  /* xyz = position, w = radius (half extent of the cube) */
    vec4 color_flags;      /* rgb = color, a = 1 lit / 2 emissive */
    vec4 prev_position;    /* xyz = position drawn last frame, w unused */
};

layout(std430, set = 0, binding = 0) readonly buffer ParticleBuffer {
    ParticleGPU particles[];
};

/* Mirror of ParticlePushConstants (engine/render/raster_gpu_types.h) */
layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float near_plane;
    float far_plane;
    int camera_inside;
} pc;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out flat int out_particle_index;

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

/* One proxy cube per particle (instance = particle index; every instance drawn is active) */
void main() {
    ParticleGPU p = particles[gl_InstanceIndex];

    /* 10% margin around the particle's cube */
    vec3 world_pos = p.position_radius.xyz + CUBE_VERTICES[gl_VertexIndex] * p.position_radius.w * 1.1;

    gl_Position = pc.view_proj * vec4(world_pos, 1.0);
    if (pc.camera_inside != 0) {
        gl_Position.z = 0.0;
    }
    out_world_pos = world_pos;
    out_particle_index = gl_InstanceIndex;
}
