#include "lighting_pass.h"
#include "vk_pipeline.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    /* Set 0 bindings (deferred_lighting.frag): the G-buffer, the shadow and AO results, the
     * material palette */
    static constexpr uint32_t SHADOW_BINDING = RenderTargets::GBUFFER_COUNT;
    static constexpr uint32_t AO_BINDING = RenderTargets::GBUFFER_COUNT + 1;
    static constexpr uint32_t PALETTE_BINDING = RenderTargets::GBUFFER_COUNT + 2;

    bool LightingPass::create(VulkanContext &ctx, const FrameResources &frames, VkFormat color_format)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        const BindingDesc sampled{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT};
        const BindingDesc uniform{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT};

        if (!create_set_layout(device, {sampled, sampled, sampled, sampled, sampled, sampled, uniform}, &set_layout_) ||
            !create_pipeline_layout(device, {set_layout_, frames.lighting_layout}, VK_SHADER_STAGE_FRAGMENT_BIT,
                                    sizeof(VoxelPushConstants), &layout_))
        {
            fprintf(stderr, "Failed to create the deferred lighting pipeline layout\n");
            return false;
        }

        GraphicsPipelineDesc desc{};
        desc.vertex = {shaders::k_shader_voxel_vert_spv, shaders::k_shader_voxel_vert_spv_size};
        desc.fragment = {shaders::k_shader_deferred_lighting_frag_spv, shaders::k_shader_deferred_lighting_frag_spv_size};
        desc.layout = layout_;
        desc.color_formats = &color_format;
        desc.color_count = 1;
        if (!create_graphics_pipeline(ctx, desc, &pipeline_))
        {
            fprintf(stderr, "Failed to create deferred lighting pipeline\n");
            return false;
        }

        if (!create_descriptor_pool(device, MAX_FRAMES_IN_FLIGHT,
                                    {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * (AO_BINDING + 1)},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT}},
                                    &pool_) ||
            !allocate_sets(device, pool_, set_layout_, MAX_FRAMES_IN_FLIGHT, sets_))
        {
            fprintf(stderr, "Failed to allocate deferred lighting descriptor sets\n");
            return false;
        }
        if (!create_white_input())
        {
            fprintf(stderr, "Failed to create the lighting's white input\n");
            return false;
        }
        return true;
    }

    bool LightingPass::create_white_input()
    {
        ImageDesc desc;
        desc.format = VK_FORMAT_R8_UNORM;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!ctx_->create_image(desc, &white_))
            return false;

        VkCommandBuffer cmd = ctx_->begin_immediate_commands();
        if (!cmd)
            return false;
        BarrierBatch barriers(cmd);
        barriers.discard(white_, image_use::clear_dst);
        barriers.record();
        const VkClearColorValue white = {{1.0f, 1.0f, 1.0f, 1.0f}};
        vkCmdClearColorImage(cmd, white_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &white_.range);
        barriers.transition(white_, image_use::sampled(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT));
        barriers.record();
        return ctx_->submit_immediate_commands(cmd);
    }

    void LightingPass::destroy()
    {
        if (!ctx_)
            return;
        VkDevice device = ctx_->device();
        destroy_descriptor_pool(device, &pool_);
        destroy_pipeline(device, &pipeline_);
        destroy_pipeline_layout(device, &layout_);
        destroy_set_layout(device, &set_layout_);
        ctx_->destroy_image(&white_);
    }

    void LightingPass::bind_targets(const RenderTargets &targets)
    {
        sampler_ = targets.sampler;
        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            for (uint32_t g = 0; g < RenderTargets::GBUFFER_COUNT; g++)
                writer.sampled(sets_[i], g, sampler_, targets.gbuffer[g].view);
            writer.sampled(sets_[i], SHADOW_BINDING, sampler_, targets.shadow.view)
                .sampled(sets_[i], AO_BINDING, sampler_, targets.ao.view);
        }
        writer.update();
    }

    void LightingPass::bind_scene(const TerrainVolume &terrain)
    {
        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            writer.buffer(sets_[i], PALETTE_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, terrain.material_palette());
        writer.update();
    }

    void LightingPass::set_shadow_input(uint32_t slot, VkImageView view)
    {
        set_input(slot, SHADOW_BINDING, view);
    }

    void LightingPass::set_ao_input(uint32_t slot, VkImageView view)
    {
        set_input(slot, AO_BINDING, view);
    }

    void LightingPass::set_input(uint32_t slot, uint32_t binding, VkImageView view)
    {
        DescriptorWriter(ctx_->device()).sampled(sets_[slot], binding, sampler_, view).update();
    }

    void LightingPass::draw(const FrameContext &frame, VkDescriptorSet lighting_set, const VoxelPushConstants &pc)
    {
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkDescriptorSet sets[2] = {sets_[frame.slot], lighting_set};
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
        vkCmdPushConstants(frame.cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0); /* fullscreen triangle */
    }

    void LightingPass::draw_to_lit_color(const FrameContext &frame, RenderTargets &targets, VkDescriptorSet lighting_set,
                                         const VoxelPushConstants &pc, const VkClearColorValue &clear)
    {
        BarrierBatch barriers(frame.cmd);
        barriers.discard(targets.lit_color, image_use::color_attachment_write);
        barriers.record();

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = targets.lit_color.view;
        color.imageLayout = targets.lit_color.state.layout;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = clear;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = targets.extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;

        vkCmdBeginRendering(frame.cmd, &rendering);
        cmd_set_viewport_scissor(frame.cmd, targets.extent);
        draw(frame, lighting_set, pc);
        vkCmdEndRendering(frame.cmd);

        /* Read by the denoise or the TAA resolve */
        barriers.transition(targets.lit_color, image_use::sampled(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT));
        barriers.record();
    }

}
