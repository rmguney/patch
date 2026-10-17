#ifndef PATCH_RENDER_RASTER_GPU_TYPES_H
#define PATCH_RENDER_RASTER_GPU_TYPES_H

#include "engine/core/types.h"
#include <cstdint>

namespace patch
{

    /*
     * What the raster G-buffer passes (voxel objects, particles) hand their shaders. No Vulkan
     * here: tests/test_gpu_layout.cpp checks these against the GLSL declarations.
     *
     * Both passes draw every proxy cube in one of two draws (camera_inside): the cubes the camera
     * is outside of with their front faces, and those it is inside of, or so close to that the
     * near plane would clip their front faces (within near_clip_radius), with their back faces.
     */

    /* push_constant block of voxel_object.vert / voxel_object.frag */
    struct VoxelObjectPushConstants
    {
        Mat4 view_proj;
        float camera_pos[3];
        float near_plane;
        float far_plane;
        int32_t debug_mode;
        float near_clip_radius; /* the near plane cuts the view this close to the camera */
        int32_t camera_inside;  /* this draw takes the cubes the camera is inside of */
    };
    static_assert(sizeof(VoxelObjectPushConstants) == 96, "VoxelObjectPushConstants must match the GLSL block");

    /* One particle of the instance buffer particle.vert / particle.frag read (std430) */
    struct ParticleGPU
    {
        float position[3];
        float radius; /* half extent of the cube */
        float color[3];
        float flags;            /* 1 = lit, 2 = emissive */
        float prev_position[4]; /* xyz: where it was drawn last frame (motion vectors); w unused */
    };
    static_assert(sizeof(ParticleGPU) == 48, "ParticleGPU must match the GLSL struct");

    /* push_constant block of particle.vert / particle.frag */
    struct ParticlePushConstants
    {
        Mat4 view_proj;
        float camera_pos[3];
        float near_plane;
        float far_plane;
        int32_t camera_inside; /* this draw takes the cubes the camera is inside of */
    };
    static_assert(sizeof(ParticlePushConstants) == 88, "ParticlePushConstants must match the GLSL block");

}

#endif
