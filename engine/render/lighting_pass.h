#ifndef PATCH_RENDER_LIGHTING_PASS_H
#define PATCH_RENDER_LIGHTING_PASS_H

#include <vulkan/vulkan.h>
#include "engine/render/frame_resources.h"
#include "engine/render/render_targets.h"
#include "engine/render/terrain_volume.h"
#include "engine/render/voxel_push_constants.h"

namespace patch
{

    /* Deferred lighting: a fullscreen pass over the G-buffer, shadow and AO results, into the
     * lit colour target (for TAA / denoise) or straight into the swapchain image */
    class LightingPass
    {
    public:
        bool create(VulkanContext &ctx, const FrameResources &frames, VkFormat color_format);
        void destroy();

        /* Also points the shadow / AO inputs back at the raw trace outputs, which is what the
         * lighting samples until the temporal resolves first run */
        void bind_targets(const RenderTargets &targets);
        /* The material palette (water absorption) */
        void bind_scene(const TerrainVolume &terrain);

        /* Frame slot's resolved shadow / AO image (that slot's previous frame has completed) */
        void set_shadow_input(uint32_t slot, VkImageView view);
        void set_ao_input(uint32_t slot, VkImageView view);

        /* One white texel: the shadow or AO input of a pass that is off (lit, unoccluded) */
        VkImageView white_input() const { return white_.view; }

        /* Into the colour attachment of the current rendering scope */
        void draw(const FrameContext &frame, VkDescriptorSet lighting_set, const VoxelPushConstants &pc);

        /* Into targets.lit_color, left readable by the denoise and TAA passes */
        void draw_to_lit_color(const FrameContext &frame, RenderTargets &targets, VkDescriptorSet lighting_set,
                               const VoxelPushConstants &pc, const VkClearColorValue &clear);

    private:
        void set_input(uint32_t slot, uint32_t binding, VkImageView view);
        bool create_white_input();

        VulkanContext *ctx_ = nullptr;
        GpuImage white_;
        VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet sets_[MAX_FRAMES_IN_FLIGHT] = {};
        VkSampler sampler_ = VK_NULL_HANDLE;
    };

}

#endif
