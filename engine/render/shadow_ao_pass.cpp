#include "shadow_ao_pass.h"
#include "voxel_objects.h"
#include "gpu_bvh.h"
#include "vk_pipeline.h"
#include "shaders_embedded.h"
#include <cmath>
#include <cstdio>

namespace patch
{

    /* Raw and resolved results are read by the resolve (compute) and the lighting pass (fragment) */
    static constexpr VkPipelineStageFlags2 RESULT_READERS =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    /* Resolve input set bindings (temporal_shadow.comp / temporal_ao.comp) */
    static constexpr uint32_t RESOLVE_DEPTH_BINDING = 0;
    static constexpr uint32_t RESOLVE_MOTION_BINDING = 1;
    static constexpr uint32_t RESOLVE_CURRENT_BINDING = 2;
    static constexpr uint32_t RESOLVE_HISTORY_BINDING = 3;
    static constexpr uint32_t RESOLVE_TEMPORAL_BINDING = 4;

    static uint32_t dispatch_groups(uint32_t size)
    {
        return (size + 7) / 8;
    }

    bool ShadowAoPass::create(VulkanContext &ctx, const FrameResources &frames)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        const VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
        const BindingDesc sampled{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, compute};
        const BindingDesc storage_image{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, compute};
        const BindingDesc storage_buffer{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, compute};
        const uint32_t push_size = sizeof(VoxelPushConstants);

        if (!create_set_layout(device, {sampled, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, compute}}, &input_layout_) ||
            !create_set_layout(device, {sampled, sampled, sampled}, &gbuffer_layout_) ||
            !create_set_layout(device, {storage_image}, &output_layout_) ||
            !create_set_layout(device, {sampled, storage_buffer, storage_buffer}, &objects_layout_) ||
            !create_set_layout(device, {sampled, sampled, sampled, sampled, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, compute}},
                               &resolve_input_layout_) ||
            !create_set_layout(device, {storage_image}, &resolve_output_layout_) ||
            !create_pipeline_layout(device, {input_layout_, gbuffer_layout_, output_layout_, objects_layout_, frames.lighting_layout},
                                    compute, push_size, &shadow_layout_) ||
            !create_pipeline_layout(device, {input_layout_, gbuffer_layout_, output_layout_}, compute, push_size, &ao_layout_) ||
            !create_pipeline_layout(device, {resolve_input_layout_, resolve_output_layout_}, compute, push_size,
                                    &resolve_layout_))
        {
            fprintf(stderr, "Failed to create the shadow / AO pipeline layouts\n");
            return false;
        }

        if (!create_compute_pipeline(ctx, {shaders::k_shader_raymarch_shadow_comp_spv, shaders::k_shader_raymarch_shadow_comp_spv_size},
                                     shadow_layout_, &shadow_pipeline_) ||
            !create_compute_pipeline(ctx, {shaders::k_shader_raymarch_ao_comp_spv, shaders::k_shader_raymarch_ao_comp_spv_size},
                                     ao_layout_, &ao_pipeline_) ||
            !create_compute_pipeline(ctx, {shaders::k_shader_temporal_shadow_comp_spv, shaders::k_shader_temporal_shadow_comp_spv_size},
                                     resolve_layout_, &shadow_resolve_.pipeline) ||
            !create_compute_pipeline(ctx, {shaders::k_shader_temporal_ao_comp_spv, shaders::k_shader_temporal_ao_comp_spv_size},
                                     resolve_layout_, &ao_resolve_.pipeline))
        {
            fprintf(stderr, "Failed to create the shadow / AO compute pipelines\n");
            return false;
        }

        /* Single sets: input (scene), G-buffer and both raw outputs (targets). Per frame: the
         * object sets and both resolves' sets, which are rewritten every frame. */
        constexpr uint32_t F = MAX_FRAMES_IN_FLIGHT;
        if (!create_descriptor_pool(device, 4 + F + 4 * F,
                                    {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 + 3 + F + 2 * F * 4},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 + 2 * F},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 + 2 * F},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * F}},
                                    &pool_) ||
            !allocate_sets(device, pool_, input_layout_, 1, &input_set_) ||
            !allocate_sets(device, pool_, gbuffer_layout_, 1, &gbuffer_set_) ||
            !allocate_sets(device, pool_, output_layout_, 1, &shadow_output_set_) ||
            !allocate_sets(device, pool_, output_layout_, 1, &ao_output_set_) ||
            !allocate_sets(device, pool_, objects_layout_, F, objects_sets_) ||
            !allocate_sets(device, pool_, resolve_input_layout_, F, shadow_resolve_.input_sets) ||
            !allocate_sets(device, pool_, resolve_output_layout_, F, shadow_resolve_.output_sets) ||
            !allocate_sets(device, pool_, resolve_input_layout_, F, ao_resolve_.input_sets) ||
            !allocate_sets(device, pool_, resolve_output_layout_, F, ao_resolve_.output_sets))
        {
            fprintf(stderr, "Failed to allocate the shadow / AO descriptor sets\n");
            return false;
        }

        /* Last frame's camera (temporal_ao.comp), per frame like the UBO */
        DescriptorWriter writer(device);
        for (uint32_t i = 0; i < F; i++)
        {
            for (Resolve *r : {&shadow_resolve_, &ao_resolve_})
                writer.buffer(r->input_sets[i], RESOLVE_TEMPORAL_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              frames.temporal_ubo[i].buffer, sizeof(VoxelTemporalUBO));
        }
        writer.update();

        if (!create_blue_noise())
        {
            fprintf(stderr, "Failed to create blue noise texture\n");
            return false;
        }
        return true;
    }

    void ShadowAoPass::destroy()
    {
        if (!ctx_)
            return;
        VkDevice device = ctx_->device();
        destroy_descriptor_pool(device, &pool_);
        destroy_pipeline(device, &shadow_pipeline_);
        destroy_pipeline(device, &ao_pipeline_);
        destroy_pipeline(device, &shadow_resolve_.pipeline);
        destroy_pipeline(device, &ao_resolve_.pipeline);
        destroy_pipeline_layout(device, &shadow_layout_);
        destroy_pipeline_layout(device, &ao_layout_);
        destroy_pipeline_layout(device, &resolve_layout_);
        VkDescriptorSetLayout *layouts[] = {&input_layout_, &gbuffer_layout_, &output_layout_, &objects_layout_,
                                            &resolve_input_layout_, &resolve_output_layout_};
        for (VkDescriptorSetLayout *layout : layouts)
            destroy_set_layout(device, layout);
        destroy_sampler(device, &blue_noise_sampler_);
        ctx_->destroy_image(&blue_noise_);
    }

    bool ShadowAoPass::create_blue_noise()
    {
        constexpr uint32_t NOISE_SIZE = 128;
        constexpr VkDeviceSize DATA_SIZE = NOISE_SIZE * NOISE_SIZE;

        ImageDesc desc;
        desc.format = VK_FORMAT_R8_UNORM;
        desc.extent = {NOISE_SIZE, NOISE_SIZE, 1};
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!ctx_->create_image(desc, &blue_noise_) ||
            !create_point_sampler(ctx_->device(), VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.0f, &blue_noise_sampler_))
            return false;

        VulkanBuffer staging{};
        if (!ctx_->create_buffer(DATA_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging))
            return false;

        uint8_t *noise = static_cast<uint8_t *>(staging.mapped);
        for (uint32_t y = 0; y < NOISE_SIZE; y++)
        {
            for (uint32_t x = 0; x < NOISE_SIZE; x++)
            {
                float fx = static_cast<float>(x) + 0.5f;
                float fy = static_cast<float>(y) + 0.5f;
                float ign = fmodf(52.9829189f * fmodf(0.06711056f * fx + 0.00583715f * fy, 1.0f), 1.0f);
                noise[y * NOISE_SIZE + x] = static_cast<uint8_t>(ign * 255.0f);
            }
        }

        VkCommandBuffer cmd = ctx_->begin_immediate_commands();
        if (cmd)
        {
            BarrierBatch barriers(cmd);
            barriers.discard(blue_noise_, image_use::copy_dst);
            barriers.record();

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {NOISE_SIZE, NOISE_SIZE, 1};
            vkCmdCopyBufferToImage(cmd, staging.buffer, blue_noise_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barriers.transition(blue_noise_, image_use::sampled(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
            barriers.record();
        }
        const bool uploaded = cmd && ctx_->submit_immediate_commands(cmd);
        ctx_->destroy_buffer(&staging);
        if (!uploaded)
            return false;

        printf("  Blue noise texture created: %ux%u\n", NOISE_SIZE, NOISE_SIZE);
        return true;
    }

    void ShadowAoPass::bind_targets(const RenderTargets &targets)
    {
        const VkSampler sampler = targets.sampler;
        DescriptorWriter writer(ctx_->device());
        writer.sampled(gbuffer_set_, 0, sampler, targets.gbuffer[RenderTargets::GBUFFER_LINEAR_DEPTH].view)
            .sampled(gbuffer_set_, 1, sampler, targets.gbuffer[RenderTargets::GBUFFER_NORMAL].view)
            .sampled(gbuffer_set_, 2, blue_noise_sampler_, blue_noise_.view)
            .storage(shadow_output_set_, 0, targets.shadow.view)
            .storage(ao_output_set_, 0, targets.ao.view);

        /* The history binding and the output are chosen per frame by resolve() */
        const struct
        {
            const Resolve &resolve;
            VkImageView current;
        } resolves[] = {{shadow_resolve_, targets.shadow.view}, {ao_resolve_, targets.ao.view}};
        for (const auto &r : resolves)
        {
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                writer.sampled(r.resolve.input_sets[i], RESOLVE_DEPTH_BINDING, sampler,
                               targets.gbuffer[RenderTargets::GBUFFER_LINEAR_DEPTH].view)
                    .sampled(r.resolve.input_sets[i], RESOLVE_MOTION_BINDING, sampler, targets.motion.view)
                    .sampled(r.resolve.input_sets[i], RESOLVE_CURRENT_BINDING, sampler, r.current);
            }
        }
        writer.update();
    }

    void ShadowAoPass::bind_scene(const TerrainVolume &terrain)
    {
        /* The AO trace leaves the palette binding unused */
        DescriptorWriter writer(ctx_->device());
        writer.sampled(input_set_, 0, terrain.shadow_volume_sampler(), terrain.shadow_volume_view())
            .buffer(input_set_, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, terrain.material_palette())
            .update();
    }

    void ShadowAoPass::bind_objects(const VoxelObjects &objects)
    {
        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            writer.sampled(objects_sets_[i], 0, objects.atlas_sampler(), objects.atlas_view())
                .buffer(objects_sets_[i], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, objects.metadata(i))
                .buffer(objects_sets_[i], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, objects.bvh(i), sizeof(GPUBVHBuffer));
        }
        writer.update();
        objects_bound_ = true;
    }

    void ShadowAoPass::reset_history()
    {
        for (Resolve *r : {&shadow_resolve_, &ao_resolve_})
        {
            r->history.reset();
            r->write_index = 0;
        }
    }

    bool ShadowAoPass::trace_shadows(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc,
                                     VkDescriptorSet lighting_set)
    {
        if (!objects_bound_)
            return false;

        frame.timestamp(GpuTimestamp::SHADOW_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        BarrierBatch barriers(frame.cmd);
        barriers.discard(targets.shadow, image_use::compute_write);
        barriers.record();

        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_pipeline_);
        const VkDescriptorSet sets[5] = {input_set_, gbuffer_set_, shadow_output_set_, objects_sets_[frame.slot], lighting_set};
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_layout_, 0, 5, sets, 0, nullptr);
        /* object_count bounds the BVH's object indices to the uploaded metadata */
        vkCmdPushConstants(frame.cmd, shadow_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(frame.cmd, dispatch_groups(targets.extent.width), dispatch_groups(targets.extent.height), 1);

        frame.timestamp(GpuTimestamp::SHADOW_END, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        barriers.transition(targets.shadow, image_use::sampled(RESULT_READERS));
        barriers.record();
        return true;
    }

    void ShadowAoPass::trace_ao(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc)
    {
        frame.timestamp(GpuTimestamp::AO_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        BarrierBatch barriers(frame.cmd);
        barriers.discard(targets.ao, image_use::compute_write);
        barriers.record();

        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ao_pipeline_);
        const VkDescriptorSet sets[3] = {input_set_, gbuffer_set_, ao_output_set_};
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ao_layout_, 0, 3, sets, 0, nullptr);
        vkCmdPushConstants(frame.cmd, ao_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(frame.cmd, dispatch_groups(targets.extent.width), dispatch_groups(targets.extent.height), 1);

        frame.timestamp(GpuTimestamp::AO_END, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        barriers.transition(targets.ao, image_use::sampled(RESULT_READERS));
        barriers.record();
    }

    VkImageView ShadowAoPass::resolve_shadows(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc)
    {
        return resolve(frame, shadow_resolve_, targets.shadow_history, targets.sampler, targets.extent, pc,
                       GpuTimestamp::TEMPORAL_SHADOW_BEGIN, GpuTimestamp::TEMPORAL_SHADOW_END);
    }

    VkImageView ShadowAoPass::resolve_ao(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc)
    {
        return resolve(frame, ao_resolve_, targets.ao_history, targets.sampler, targets.extent, pc,
                       GpuTimestamp::TEMPORAL_AO_BEGIN, GpuTimestamp::TEMPORAL_AO_END);
    }

    /* The frame slot's sets are rewritten to read history[read] and write history[write]; that
     * slot's previous frame has completed (begin_frame() waited for its fence) */
    VkImageView ShadowAoPass::resolve(const FrameContext &frame, Resolve &r, GpuImage (&history)[2], VkSampler sampler,
                                      VkExtent2D extent, const VoxelPushConstants &pc, GpuTimestamp begin, GpuTimestamp end)
    {
        const int write_index = r.write_index & 1;
        const int read_index = 1 - write_index;
        GpuImage &target = history[write_index];

        BarrierBatch barriers(frame.cmd);
        barriers.discard(target, image_use::compute_write);
        barriers.record();

        DescriptorWriter writer(ctx_->device());
        writer.sampled(r.input_sets[frame.slot], RESOLVE_HISTORY_BINDING, sampler, history[read_index].view)
            .storage(r.output_sets[frame.slot], 0, target.view)
            .update();

        frame.timestamp(begin, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
        const VkDescriptorSet sets[2] = {r.input_sets[frame.slot], r.output_sets[frame.slot]};
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolve_layout_, 0, 2, sets, 0, nullptr);
        VoxelPushConstants resolve_pc = pc;
        resolve_pc.history_valid = r.history.valid(frame) ? 1 : 0;
        vkCmdPushConstants(frame.cmd, resolve_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(resolve_pc), &resolve_pc);
        vkCmdDispatch(frame.cmd, dispatch_groups(extent.width), dispatch_groups(extent.height), 1);

        frame.timestamp(end, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        barriers.transition(target, image_use::sampled(RESULT_READERS));
        barriers.record();

        r.history.written(frame);
        r.write_index = read_index;
        return target.view;
    }

}
