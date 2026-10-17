#ifndef VOXEL_PUSH_CONSTANTS_GLSL
#define VOXEL_PUSH_CONSTANTS_GLSL

/*
 * Mirror of patch::VoxelPushConstants (engine/render/voxel_push_constants.h), shared by
 * the raymarching compute passes and the deferred lighting pass. 232 bytes, std430:
 * each vec3 is followed by a scalar so no padding is inserted.
 */
layout(push_constant) uniform VoxelPushConstants {
    mat4 inv_view;
    mat4 inv_projection;
    vec3 bounds_min;
    float voxel_size;
    vec3 bounds_max;
    int max_steps;
    vec3 camera_pos;
    int history_valid;
    ivec3 grid_size;
    int total_chunks;
    ivec3 chunks_dim;
    int frame_count;      /* frame index modulo 65536 */
    int debug_mode;
    float far_plane;
    int object_count;     /* voxel objects in the metadata buffer (bounds BVH indices) */
    int shadow_quality;
    int shadow_contact;
    int ao_quality;
} pc;

#endif /* VOXEL_PUSH_CONSTANTS_GLSL */
