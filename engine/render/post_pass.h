#ifndef PATCH_RENDER_POST_PASS_H
#define PATCH_RENDER_POST_PASS_H

#include <vulkan/vulkan.h>
#include "engine/render/frame_resources.h"
#include "engine/render/render_targets.h"
#include "engine/render/voxel_push_constants.h"

namespace patch
{

    /*
     * After lighting into lit_color: an edge-aware spatial denoise, then TAA (blend with the
     * reprojected history, ping-pong between two history images) into the swapchain image.
     * The denoise runs first, on the jittered frame its depth and normals belong to; with TAA
     * off it draws straight into the swapchain image.
     */
    class PostPass
    {
    public:
        /* color_format: the swapchain's, which both passes draw into */
        bool create(VulkanContext &ctx, VkFormat color_format);
        void destroy();

        void bind_targets(const RenderTargets &targets);

        /* Next TAA resolve starts without history, writing history image 0 */
        void reset_history();

        /* lit_color into the colour attachment of the current rendering scope, every pixel of it */
        void denoise(const FrameContext &frame, const VoxelPushConstants &pc);
        /* lit_color into targets.denoised, for the TAA */
        void denoise_for_taa(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc);

        /* input (lit_color or denoised) and the history into the swapchain image (every pixel)
         * and the next history; the rendering scope is closed again */
        void resolve_taa(const FrameContext &frame, RenderTargets &targets, const GpuImage &input,
                         TrackedImage &swapchain_image, VkImageView swapchain_view, const VoxelPushConstants &pc);

    private:
        VulkanContext *ctx_ = nullptr;

        /* TAA: current frame, history, motion, depth in; display image and history out */
        VkDescriptorSetLayout taa_input_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout taa_layout_ = VK_NULL_HANDLE;
        VkPipeline taa_pipeline_ = VK_NULL_HANDLE;
        /* The reprojected history falls between texels: it is filtered */
        VkSampler history_sampler_ = VK_NULL_HANDLE;

        /* Denoise: depth, normal, lit colour in; the colour attachment out */
        VkDescriptorSetLayout denoise_input_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout denoise_layout_ = VK_NULL_HANDLE;
        VkPipeline denoise_pipeline_ = VK_NULL_HANDLE;

        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet taa_input_sets_[MAX_FRAMES_IN_FLIGHT] = {}; /* input and history rewritten per frame */
        VkDescriptorSet denoise_input_set_ = VK_NULL_HANDLE;

        int taa_write_index_ = 0;
        TemporalHistory taa_history_;
    };

}

#endif
