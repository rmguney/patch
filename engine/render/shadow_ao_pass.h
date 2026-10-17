#ifndef PATCH_RENDER_SHADOW_AO_PASS_H
#define PATCH_RENDER_SHADOW_AO_PASS_H

#include <vulkan/vulkan.h>
#include "engine/render/frame_resources.h"
#include "engine/render/render_targets.h"
#include "engine/render/terrain_volume.h"
#include "engine/render/voxel_push_constants.h"

namespace patch
{

    class VoxelObjects;

    /*
     * Screen-space shadow and AO: each traces the terrain shadow volume from the G-buffer (the
     * shadow trace also intersects the voxel objects through their BVH), then a temporal resolve
     * blends the result with its reprojected history. The resolves ping-pong between two history
     * images; the image just written is what the lighting pass samples. The AO history keeps the
     * linear depth each pixel was resolved at, for its disocclusion test.
     */
    class ShadowAoPass
    {
    public:
        bool create(VulkanContext &ctx, const FrameResources &frames);
        void destroy();

        void bind_targets(const RenderTargets &targets);
        void bind_scene(const TerrainVolume &terrain);
        /* The shadow trace needs the object atlas, metadata and BVH */
        void bind_objects(const VoxelObjects &objects);
        void unbind_objects() { objects_bound_ = false; }

        /* Next resolves start without history, writing history image 0 */
        void reset_history();

        /* False (nothing recorded) without bound objects */
        bool trace_shadows(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc,
                           VkDescriptorSet lighting_set);
        /* Returns the resolved image for the lighting pass */
        VkImageView resolve_shadows(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc);

        void trace_ao(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc);
        VkImageView resolve_ao(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc);

    private:
        struct Resolve
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkDescriptorSet input_sets[MAX_FRAMES_IN_FLIGHT] = {};
            VkDescriptorSet output_sets[MAX_FRAMES_IN_FLIGHT] = {};
            int write_index = 0;
            TemporalHistory history;
        };

        VkImageView resolve(const FrameContext &frame, Resolve &resolve, GpuImage (&history)[2], VkSampler sampler,
                            VkExtent2D extent, const VoxelPushConstants &pc, GpuTimestamp begin, GpuTimestamp end);
        bool create_blue_noise();

        VulkanContext *ctx_ = nullptr;

        /* Shared by the shadow (sets 0-4) and AO (sets 0-2) traces */
        VkDescriptorSetLayout input_layout_ = VK_NULL_HANDLE;   /* set 0: shadow volume, material palette */
        VkDescriptorSetLayout gbuffer_layout_ = VK_NULL_HANDLE; /* set 1: depth, normal, world pos, blue noise */
        VkDescriptorSetLayout output_layout_ = VK_NULL_HANDLE;  /* set 2: raw result */
        VkDescriptorSetLayout objects_layout_ = VK_NULL_HANDLE; /* set 3: object atlas, metadata, BVH */
        VkPipelineLayout shadow_layout_ = VK_NULL_HANDLE;       /* set 4: scene lighting */
        VkPipelineLayout ao_layout_ = VK_NULL_HANDLE;
        VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
        VkPipeline ao_pipeline_ = VK_NULL_HANDLE;

        /* Shared by both resolves */
        VkDescriptorSetLayout resolve_input_layout_ = VK_NULL_HANDLE;  /* depth, motion, current, history, temporal UBO */
        VkDescriptorSetLayout resolve_output_layout_ = VK_NULL_HANDLE; /* resolved result */
        VkPipelineLayout resolve_layout_ = VK_NULL_HANDLE;
        Resolve shadow_resolve_;
        Resolve ao_resolve_;

        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet input_set_ = VK_NULL_HANDLE;
        VkDescriptorSet gbuffer_set_ = VK_NULL_HANDLE;
        VkDescriptorSet shadow_output_set_ = VK_NULL_HANDLE;
        VkDescriptorSet ao_output_set_ = VK_NULL_HANDLE;
        VkDescriptorSet objects_sets_[MAX_FRAMES_IN_FLIGHT] = {}; /* per frame, like the metadata and BVH */
        bool objects_bound_ = false;

        /* Interleaved gradient noise that decorrelates the trace samples */
        GpuImage blue_noise_;
        VkSampler blue_noise_sampler_ = VK_NULL_HANDLE;
    };

}

#endif
