#include "post_pass.h"
#include "vk_pipeline.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    /* Input set bindings: taa_resolve.frag (current, history, motion, depth) and
     * spatial_denoise.frag (depth, normal, colour) */
    static constexpr uint32_t TAA_CURRENT_BINDING = 0;
    static constexpr uint32_t TAA_HISTORY_BINDING = 1;
    static constexpr uint32_t TAA_MOTION_BINDING = 2;
    static constexpr uint32_t TAA_DEPTH_BINDING = 3;

    static constexpr ImageState READ_BY_FRAGMENT = image_use::sampled(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    bool PostPass::create(VulkanContext &ctx, VkFormat color_format)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        const VkShaderStageFlags fragment = VK_SHADER_STAGE_FRAGMENT_BIT;
        const BindingDesc sampled{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fragment};

        if (!create_set_layout(device, {sampled, sampled, sampled, sampled}, &taa_input_layout_) ||
            !create_pipeline_layout(device, {taa_input_layout_}, fragment, sizeof(VoxelPushConstants), &taa_layout_) ||
            !create_linear_sampler(device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, &history_sampler_))
        {
            fprintf(stderr, "Failed to create the TAA pipeline layout\n");
            return false;
        }

        /* Location 0: the display image, location 1: the next history */
        const VkFormat taa_formats[2] = {color_format, RenderTargets::TAA_HISTORY_FORMAT};
        GraphicsPipelineDesc taa{};
        taa.vertex = {shaders::k_shader_voxel_vert_spv, shaders::k_shader_voxel_vert_spv_size};
        taa.fragment = {shaders::k_shader_taa_resolve_frag_spv, shaders::k_shader_taa_resolve_frag_spv_size};
        taa.layout = taa_layout_;
        taa.color_formats = taa_formats;
        taa.color_count = 2;
        if (!create_graphics_pipeline(ctx, taa, &taa_pipeline_))
        {
            fprintf(stderr, "Failed to create the TAA pipeline\n");
            return false;
        }

        if (!create_set_layout(device, {sampled, sampled, sampled}, &denoise_input_layout_) ||
            !create_pipeline_layout(device, {denoise_input_layout_}, fragment, sizeof(VoxelPushConstants), &denoise_layout_))
        {
            fprintf(stderr, "Failed to create the denoise pipeline layout\n");
            return false;
        }

        /* Draws into the swapchain image or into denoised, which shares its format */
        GraphicsPipelineDesc denoise{};
        denoise.vertex = {shaders::k_shader_voxel_vert_spv, shaders::k_shader_voxel_vert_spv_size};
        denoise.fragment = {shaders::k_shader_spatial_denoise_frag_spv, shaders::k_shader_spatial_denoise_frag_spv_size};
        denoise.layout = denoise_layout_;
        denoise.color_formats = &color_format;
        denoise.color_count = 1;
        if (!create_graphics_pipeline(ctx, denoise, &denoise_pipeline_))
        {
            fprintf(stderr, "Failed to create the denoise pipeline\n");
            return false;
        }

        constexpr uint32_t F = MAX_FRAMES_IN_FLIGHT;
        if (!create_descriptor_pool(device, F + 1, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * F + 3}}, &pool_) ||
            !allocate_sets(device, pool_, taa_input_layout_, F, taa_input_sets_) ||
            !allocate_sets(device, pool_, denoise_input_layout_, 1, &denoise_input_set_))
        {
            fprintf(stderr, "Failed to allocate the TAA / denoise descriptor sets\n");
            return false;
        }
        return true;
    }

    void PostPass::destroy()
    {
        if (!ctx_)
            return;
        VkDevice device = ctx_->device();
        destroy_descriptor_pool(device, &pool_);
        destroy_pipeline(device, &taa_pipeline_);
        destroy_pipeline(device, &denoise_pipeline_);
        destroy_pipeline_layout(device, &taa_layout_);
        destroy_pipeline_layout(device, &denoise_layout_);
        destroy_set_layout(device, &taa_input_layout_);
        destroy_set_layout(device, &denoise_input_layout_);
        destroy_sampler(device, &history_sampler_);
    }

    /* The TAA's current frame and history are chosen per frame */
    void PostPass::bind_targets(const RenderTargets &targets)
    {
        const VkSampler sampler = targets.sampler;
        const VkImageView depth = targets.gbuffer[RenderTargets::GBUFFER_LINEAR_DEPTH].view;
        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            writer.sampled(taa_input_sets_[i], TAA_MOTION_BINDING, sampler, targets.motion.view)
                .sampled(taa_input_sets_[i], TAA_DEPTH_BINDING, sampler, depth);
        }
        writer.sampled(denoise_input_set_, 0, sampler, depth)
            .sampled(denoise_input_set_, 1, sampler, targets.gbuffer[RenderTargets::GBUFFER_NORMAL].view)
            .sampled(denoise_input_set_, 2, sampler, targets.lit_color.view)
            .update();
    }

    void PostPass::reset_history()
    {
        taa_history_.reset();
        taa_write_index_ = 0;
    }

    void PostPass::denoise(const FrameContext &frame, const VoxelPushConstants &pc)
    {
        frame.timestamp(GpuTimestamp::DENOISE_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, denoise_pipeline_);
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, denoise_layout_, 0, 1, &denoise_input_set_, 0,
                                nullptr);
        vkCmdPushConstants(frame.cmd, denoise_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0); /* fullscreen triangle */

        frame.timestamp(GpuTimestamp::DENOISE_END, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    void PostPass::denoise_for_taa(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc)
    {
        BarrierBatch barriers(frame.cmd);
        barriers.discard(targets.denoised, image_use::color_attachment_write);
        barriers.record();

        /* Every pixel is drawn */
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = targets.denoised.view;
        color.imageLayout = targets.denoised.state.layout;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = targets.extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;

        vkCmdBeginRendering(frame.cmd, &rendering);
        cmd_set_viewport_scissor(frame.cmd, targets.extent);
        denoise(frame, pc);
        vkCmdEndRendering(frame.cmd);

        barriers.transition(targets.denoised, READ_BY_FRAGMENT);
        barriers.record();
    }

    /* The frame slot's set is rewritten to read input and history[read]; that slot's previous
     * frame has completed (begin_frame() waited for its fence) */
    void PostPass::resolve_taa(const FrameContext &frame, RenderTargets &targets, const GpuImage &input,
                               TrackedImage &swapchain_image, VkImageView swapchain_view, const VoxelPushConstants &pc)
    {
        const int write_index = taa_write_index_ & 1;
        const int read_index = 1 - write_index;
        GpuImage &history = targets.taa_history[write_index];

        /* Every pixel of both is drawn */
        BarrierBatch barriers(frame.cmd);
        barriers.discard(swapchain_image, image_use::color_attachment_write);
        barriers.discard(history, image_use::color_attachment_write);
        barriers.record();

        DescriptorWriter(ctx_->device())
            .sampled(taa_input_sets_[frame.slot], TAA_CURRENT_BINDING, targets.sampler, input.view)
            .sampled(taa_input_sets_[frame.slot], TAA_HISTORY_BINDING, history_sampler_, targets.taa_history[read_index].view)
            .update();

        VkRenderingAttachmentInfo colors[2]{};
        const VkImageView views[2] = {swapchain_view, history.view};
        for (uint32_t i = 0; i < 2; i++)
        {
            colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colors[i].imageView = views[i];
            colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = targets.extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 2;
        rendering.pColorAttachments = colors;

        frame.timestamp(GpuTimestamp::TAA_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        vkCmdBeginRendering(frame.cmd, &rendering);
        cmd_set_viewport_scissor(frame.cmd, targets.extent);
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taa_pipeline_);
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taa_layout_, 0, 1, &taa_input_sets_[frame.slot], 0,
                                nullptr);
        VoxelPushConstants taa_pc = pc;
        taa_pc.history_valid = taa_history_.valid(frame) ? 1 : 0;
        vkCmdPushConstants(frame.cmd, taa_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(taa_pc), &taa_pc);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0); /* fullscreen triangle */
        vkCmdEndRendering(frame.cmd);

        frame.timestamp(GpuTimestamp::TAA_END, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        /* Read as history by the next frame's resolve */
        barriers.transition(history, READ_BY_FRAGMENT);
        barriers.record();

        taa_history_.written(frame);
        taa_write_index_ = read_index;
    }

}
