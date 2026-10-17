#ifndef PATCH_RENDER_PARTICLE_PASS_H
#define PATCH_RENDER_PARTICLE_PASS_H

#include <vulkan/vulkan.h>
#include "engine/core/math.h"
#include "engine/physics/particles.h"
#include "engine/render/frame_resources.h"
#include "engine/render/raster_gpu_types.h"
#include "engine/sim/env_particles.h"
#include <cstdint>

namespace patch
{

    /*
     * Raymarched particles (debris and weather): one proxy cube per particle, drawn into the
     * open G-buffer rendering scope from a per-frame instance buffer. The cubes the camera is
     * outside of fill the buffer from the front and are drawn with their front faces, those it is
     * inside of (raster_gpu_types.h) fill it from the back and are drawn with their back faces.
     */
    class ParticlePass
    {
    public:
        bool create(VulkanContext &ctx, const FrameResources &frames);
        void destroy();

        /*
         * Writes this frame's instances; returns how many particles there are to draw. alpha
         * interpolates between the last two simulation steps; frame_ticks is how many steps the
         * drawn state advanced since the previous frame (the motion vectors).
         */
        uint32_t prepare(const FrameContext &frame, const ParticleSystem *sys, const EnvParticleSystem *env_sys,
                         float alpha, float frame_ticks, Vec3 camera_pos, float near_clip_radius);
        /* Draws what prepare() wrote into the open G-buffer rendering scope */
        void draw(const FrameContext &frame, ParticlePushConstants pc) const;

    private:
        VulkanContext *ctx_ = nullptr;
        /* The CPU fills frame N's buffer while the GPU may still read frame N-1's */
        VulkanBuffer instances_[MAX_FRAMES_IN_FLIGHT] = {};
        VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;        /* front faces: cubes the camera is outside of */
        VkPipeline inside_pipeline_ = VK_NULL_HANDLE; /* back faces: cubes the camera is inside of */
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet sets_[MAX_FRAMES_IN_FLIGHT] = {};
        uint32_t outside_count_ = 0; /* instances [0, outside_count_) */
        uint32_t inside_count_ = 0;  /* instances [MAX_PARTICLE_INSTANCES - inside_count_, MAX_PARTICLE_INSTANCES) */
    };

}

#endif
