#include "gbuffer_pass.h"
#include "vk_pipeline.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    /* Depth prime push constants (depth_prime.frag) */
    struct DepthPrimePushConstants
    {
        float near_plane;
        float far_plane;
    };

    /* Read by the lighting fragment shader and by the shadow, AO, temporal, TAA and denoise
     * compute passes */
    static constexpr VkPipelineStageFlags2 GBUFFER_READERS =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    bool GBufferPass::create(VulkanContext &ctx)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        const VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
        const VkShaderStageFlags fragment = VK_SHADER_STAGE_FRAGMENT_BIT;

        if (!create_set_layout(device,
                               {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, compute},          /* chunk voxels */
                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, compute},          /* chunk headers */
                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, compute},          /* material palette */
                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, compute},          /* temporal UBO */
                                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, compute}}, /* shadow volume */
                               &terrain_layout_) ||
            !create_set_layout(device,
                               {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, compute},  /* albedo */
                                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, compute},  /* normal */
                                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, compute},  /* material */
                                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, compute},  /* linear depth */
                                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, compute}}, /* motion vectors */
                               &output_layout_) ||
            !create_pipeline_layout(device, {terrain_layout_, output_layout_}, compute, sizeof(VoxelPushConstants),
                                    &trace_layout_) ||
            !create_compute_pipeline(ctx, {shaders::k_shader_raymarch_gbuffer_comp_spv, shaders::k_shader_raymarch_gbuffer_comp_spv_size},
                                     trace_layout_, &trace_pipeline_))
        {
            fprintf(stderr, "Failed to create the G-buffer compute pipeline\n");
            return false;
        }

        if (!create_set_layout(device, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fragment}}, &depth_prime_set_layout_) ||
            !create_pipeline_layout(device, {depth_prime_set_layout_}, fragment, sizeof(DepthPrimePushConstants),
                                    &depth_prime_layout_))
        {
            fprintf(stderr, "Failed to create the depth prime pipeline layout\n");
            return false;
        }

        /* Writes every pixel's depth (compare ALWAYS): the attachment's contents are never loaded */
        GraphicsPipelineDesc depth_prime{};
        depth_prime.vertex = {shaders::k_shader_voxel_vert_spv, shaders::k_shader_voxel_vert_spv_size};
        depth_prime.fragment = {shaders::k_shader_depth_prime_frag_spv, shaders::k_shader_depth_prime_frag_spv_size};
        depth_prime.layout = depth_prime_layout_;
        depth_prime.depth_format = RenderTargets::DEPTH_FORMAT;
        depth_prime.depth_test = true;
        depth_prime.depth_write = true;
        depth_prime.depth_compare = VK_COMPARE_OP_ALWAYS;
        if (!create_graphics_pipeline(ctx, depth_prime, &depth_prime_pipeline_))
        {
            fprintf(stderr, "Failed to create the depth prime pipeline\n");
            return false;
        }

        if (!create_descriptor_pool(device, MAX_FRAMES_IN_FLIGHT + 2,
                                    {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * 2},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * 2},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RenderTargets::RASTER_COLOR_COUNT},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT + 1}},
                                    &pool_) ||
            !allocate_sets(device, pool_, terrain_layout_, MAX_FRAMES_IN_FLIGHT, terrain_sets_) ||
            !allocate_sets(device, pool_, output_layout_, 1, &output_set_) ||
            !allocate_sets(device, pool_, depth_prime_set_layout_, 1, &depth_prime_set_))
        {
            fprintf(stderr, "Failed to allocate the G-buffer descriptor sets\n");
            return false;
        }
        return true;
    }

    void GBufferPass::destroy()
    {
        if (!ctx_)
            return;
        VkDevice device = ctx_->device();
        destroy_descriptor_pool(device, &pool_);
        destroy_pipeline(device, &trace_pipeline_);
        destroy_pipeline_layout(device, &trace_layout_);
        destroy_set_layout(device, &terrain_layout_);
        destroy_set_layout(device, &output_layout_);
        destroy_pipeline(device, &depth_prime_pipeline_);
        destroy_pipeline_layout(device, &depth_prime_layout_);
        destroy_set_layout(device, &depth_prime_set_layout_);
    }

    void GBufferPass::bind_targets(const RenderTargets &targets)
    {
        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < RenderTargets::GBUFFER_COUNT; i++)
            writer.storage(output_set_, i, targets.gbuffer[i].view);
        writer.storage(output_set_, RenderTargets::GBUFFER_COUNT, targets.motion.view);
        writer.sampled(depth_prime_set_, 0, targets.sampler, targets.gbuffer[RenderTargets::GBUFFER_LINEAR_DEPTH].view);
        writer.update();
    }

    void GBufferPass::bind_scene(const TerrainVolume &terrain, const FrameResources &frames)
    {
        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            writer.buffer(terrain_sets_[i], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, terrain.voxel_data())
                .buffer(terrain_sets_[i], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, terrain.chunk_headers())
                .buffer(terrain_sets_[i], 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, terrain.material_palette())
                .buffer(terrain_sets_[i], 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames.temporal_ubo[i].buffer,
                        sizeof(VoxelTemporalUBO))
                .sampled(terrain_sets_[i], 4, terrain.shadow_volume_sampler(), terrain.shadow_volume_view());
        }
        writer.update();
    }

    void GBufferPass::trace_terrain(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc)
    {
        frame.timestamp(GpuTimestamp::GBUFFER_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        /* Every pixel is rewritten: last frame's contents are discarded */
        BarrierBatch barriers(frame.cmd);
        for (uint32_t i = 0; i < RenderTargets::RASTER_COLOR_COUNT; i++)
            barriers.discard(targets.raster_color(i), image_use::compute_write);
        barriers.record();

        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, trace_pipeline_);
        const VkDescriptorSet sets[2] = {terrain_sets_[frame.slot], output_set_};
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, trace_layout_, 0, 2, sets, 0, nullptr);
        vkCmdPushConstants(frame.cmd, trace_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(frame.cmd, (targets.extent.width + 7) / 8, (targets.extent.height + 7) / 8, 1);

        frame.timestamp(GpuTimestamp::GBUFFER_END, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    void GBufferPass::prime_depth(const FrameContext &frame, RenderTargets &targets, float near_plane, float far_plane)
    {
        BarrierBatch barriers(frame.cmd);
        barriers.transition(targets.gbuffer[RenderTargets::GBUFFER_LINEAR_DEPTH],
                            image_use::sampled(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT));
        barriers.discard(targets.depth, image_use::depth_attachment);
        barriers.record();

        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = targets.depth.view;
        depth.imageLayout = targets.depth.state.layout;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = targets.extent;
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth;

        vkCmdBeginRendering(frame.cmd, &rendering);
        cmd_set_viewport_scissor(frame.cmd, targets.extent);
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_prime_pipeline_);
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_prime_layout_, 0, 1,
                                &depth_prime_set_, 0, nullptr);
        const DepthPrimePushConstants pc = {near_plane, far_plane};
        vkCmdPushConstants(frame.cmd, depth_prime_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0); /* fullscreen triangle */
        vkCmdEndRendering(frame.cmd);
    }

    void GBufferPass::begin_raster(const FrameContext &frame, RenderTargets &targets, float near_plane, float far_plane)
    {
        prime_depth(frame, targets, near_plane, far_plane);

        BarrierBatch barriers(frame.cmd);
        VkRenderingAttachmentInfo colors[RenderTargets::RASTER_COLOR_COUNT]{};
        for (uint32_t i = 0; i < RenderTargets::RASTER_COLOR_COUNT; i++)
        {
            GpuImage &image = targets.raster_color(i);
            barriers.transition(image, image_use::color_attachment);

            colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colors[i].imageView = image.view;
            colors[i].imageLayout = image.state.layout;
            colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        barriers.transition(targets.depth, image_use::depth_attachment);
        barriers.record();

        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = targets.depth.view;
        depth.imageLayout = targets.depth.state.layout;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = targets.extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = RenderTargets::RASTER_COLOR_COUNT;
        rendering.pColorAttachments = colors;
        rendering.pDepthAttachment = &depth;

        vkCmdBeginRendering(frame.cmd, &rendering);
        cmd_set_viewport_scissor(frame.cmd, targets.extent);
    }

    void GBufferPass::finish(const FrameContext &frame, RenderTargets &targets, bool rasterized)
    {
        if (rasterized)
            vkCmdEndRendering(frame.cmd);

        BarrierBatch barriers(frame.cmd);
        for (uint32_t i = 0; i < RenderTargets::RASTER_COLOR_COUNT; i++)
            barriers.transition(targets.raster_color(i), image_use::sampled(GBUFFER_READERS));
        barriers.record();
    }

}
