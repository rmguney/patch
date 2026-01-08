#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

struct VoxelObjectGPU {
    mat4 world_to_local;
    mat4 local_to_world;
    vec4 bounds_min;
    vec4 bounds_max;
    vec4 position;
    uint atlas_slice;
    uint material_base;
    uint flags;
    uint pad;
};

layout(std430, SET_BINDING(0, 1)) readonly buffer ObjectMetadata {
    VoxelObjectGPU objects[];
};

layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float pad1;
    int object_count;
    int atlas_dim;
    int pad2[2];
} pc;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec3 out_ray_origin;
layout(location = 2) out flat int out_object_index;

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
    int obj_idx = gl_InstanceIndex;

    VoxelObjectGPU obj = objects[obj_idx];

    if (obj.position.w < 0.5) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        out_world_pos = vec3(0.0);
        out_ray_origin = vec3(0.0);
        out_object_index = -1;
        return;
    }

    float half_grid = obj.bounds_max.w * 0.5;
    float expand = 1.1;
    vec3 local_pos = CUBE_VERTICES[vertex_id] * half_grid * expand;

    vec4 world_pos = obj.local_to_world * vec4(local_pos, 1.0);

    gl_Position = pc.view_proj * world_pos;
    out_world_pos = world_pos.xyz;
    out_ray_origin = pc.camera_pos;
    out_object_index = obj_idx;
}
