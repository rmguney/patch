#version 450

/* voxel_object.vert - one proxy cube per voxel object (instance = object index); the
 * fragment shader raymarches the object's voxel grid inside it. Each object is drawn by one
 * of two draws (pc.camera_inside): with the camera outside its cube, the front faces at their
 * depth; with the camera inside the cube, or so close that the near plane would clip its front
 * faces, the back faces on the near plane, which keeps the fragment shader's conservative
 * depth valid. */

#include "include/voxobj_types.glsl"

layout(std430, set = 0, binding = 1) readonly buffer ObjectMetadata {
    VoxelObjectGPU objects[];
};

/* Mirror of VoxelObjectPushConstants (engine/render/raster_gpu_types.h) */
layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float near_plane;
    float far_plane;
    int debug_mode;
    float near_clip_radius;
    int camera_inside;
} pc;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out flat int out_object_index;

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
    int obj_idx = gl_InstanceIndex;

    /* Collapsed (outside the clip volume): hidden, or drawn by the other draw */
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
    out_world_pos = vec3(0.0);
    out_object_index = -1;

    /* Members one by one: a copy of the whole struct would take its brick table along */
    if (objects[obj_idx].position.w < 0.5) {
        return;
    }

    /* Grid-sized cube in voxel units (local_to_world scales by voxel_size), 10% margin */
    float half_cube = objects[obj_idx].bounds_max.w * 0.5 * 1.1;

    /* The camera in the object's frame (world_to_local does not scale) against the cube, grown
     * by the distance within which the near plane cuts the view */
    vec3 camera_local = (objects[obj_idx].world_to_local * vec4(pc.camera_pos, 1.0)).xyz;
    float reach = half_cube * objects[obj_idx].bounds_min.w + pc.near_clip_radius;
    bool inside = all(lessThan(abs(camera_local), vec3(reach)));
    if (inside != (pc.camera_inside != 0)) {
        return;
    }

    vec3 local_pos = CUBE_VERTICES[gl_VertexIndex] * half_cube;
    vec4 world_pos = objects[obj_idx].local_to_world * vec4(local_pos, 1.0);

    gl_Position = pc.view_proj * world_pos;
    if (inside) {
        gl_Position.z = 0.0;
    }
    out_world_pos = world_pos.xyz;
    out_object_index = obj_idx;
}
