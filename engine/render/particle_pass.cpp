#include "particle_pass.h"
#include "render_targets.h"
#include "vk_pipeline.h"
#include "voxel_push_constants.h"
#include "shaders_embedded.h"
#include <cmath>
#include <cstdio>

namespace patch
{

    static constexpr uint32_t MAX_PARTICLE_INSTANCES = 65536;
    /* Half extent of a particle's proxy cube, in radii (particle.vert) */
    static constexpr float PROXY_SCALE = 1.1f;

    bool ParticlePass::create(VulkanContext &ctx, const FrameResources &frames)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        const VkShaderStageFlags vertex_fragment = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        const VkDeviceSize buffer_size = MAX_PARTICLE_INSTANCES * sizeof(ParticleGPU);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (!ctx.create_buffer(buffer_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &instances_[i]))
            {
                fprintf(stderr, "Failed to create particle SSBO\n");
                return false;
            }
        }

        if (!create_set_layout(device,
                               {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vertex_fragment},              /* instances */
                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT}}, /* temporal UBO (motion vectors) */
                               &set_layout_) ||
            !create_pipeline_layout(device, {set_layout_}, vertex_fragment, sizeof(ParticlePushConstants), &layout_) ||
            !create_descriptor_pool(device, MAX_FRAMES_IN_FLIGHT,
                                    {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT}},
                                    &pool_) ||
            !allocate_sets(device, pool_, set_layout_, MAX_FRAMES_IN_FLIGHT, sets_))
        {
            fprintf(stderr, "Failed to create particle descriptor sets\n");
            return false;
        }

        DescriptorWriter writer(device);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            writer.buffer(sets_[i], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, instances_[i].buffer, buffer_size)
                .buffer(sets_[i], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames.temporal_ubo[i].buffer,
                        sizeof(VoxelTemporalUBO));
        }
        writer.update();

        GraphicsPipelineDesc desc{};
        desc.vertex = {shaders::k_shader_particle_vert_spv, shaders::k_shader_particle_vert_spv_size};
        desc.fragment = {shaders::k_shader_particle_frag_spv, shaders::k_shader_particle_frag_spv_size};
        desc.layout = layout_;
        desc.color_formats = RenderTargets::RASTER_COLOR_FORMATS;
        desc.color_count = RenderTargets::RASTER_COLOR_COUNT;
        desc.depth_format = RenderTargets::DEPTH_FORMAT;
        desc.depth_test = true;
        desc.depth_write = true;
        desc.depth_compare = VK_COMPARE_OP_LESS;
        desc.cull_mode = VK_CULL_MODE_BACK_BIT;
        GraphicsPipelineDesc inside_desc = desc;
        inside_desc.cull_mode = VK_CULL_MODE_FRONT_BIT;
        if (!create_graphics_pipeline(ctx, desc, &pipeline_) || !create_graphics_pipeline(ctx, inside_desc, &inside_pipeline_))
        {
            fprintf(stderr, "Failed to create particle pipeline\n");
            return false;
        }
        return true;
    }

    void ParticlePass::destroy()
    {
        if (!ctx_)
            return;
        VkDevice device = ctx_->device();
        destroy_pipeline(device, &pipeline_);
        destroy_pipeline(device, &inside_pipeline_);
        destroy_pipeline_layout(device, &layout_);
        destroy_descriptor_pool(device, &pool_);
        destroy_set_layout(device, &set_layout_);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            ctx_->destroy_buffer(&instances_[i]);
    }

    uint32_t ParticlePass::prepare(const FrameContext &frame, const ParticleSystem *sys, const EnvParticleSystem *env_sys,
                                   float alpha, float frame_ticks, Vec3 camera_pos, float near_clip_radius)
    {
        ParticleGPU *gpu_data = static_cast<ParticleGPU *>(instances_[frame.slot].mapped);
        outside_count_ = 0;
        inside_count_ = 0;

        /* Drawn between the last two simulation steps; last frame drew the same blend
         * frame_ticks steps earlier (extrapolated along the last step past its start) */
        const float prev_alpha = alpha - frame_ticks;
        auto add = [&](Vec3 prev, Vec3 current, float radius, Vec3 color, float flags)
        {
            const Vec3 position = vec3_lerp(prev, current, alpha);
            const Vec3 drawn_before = vec3_lerp(prev, current, prev_alpha);

            /* The camera inside the proxy cube, or close enough for the near plane to cut it */
            const float reach = radius * PROXY_SCALE + near_clip_radius;
            const bool inside = fabsf(position.x - camera_pos.x) < reach && fabsf(position.y - camera_pos.y) < reach &&
                                fabsf(position.z - camera_pos.z) < reach;
            ParticleGPU &g = inside ? gpu_data[MAX_PARTICLE_INSTANCES - ++inside_count_] : gpu_data[outside_count_++];
            g.position[0] = position.x;
            g.position[1] = position.y;
            g.position[2] = position.z;
            g.radius = radius;
            g.color[0] = color.x;
            g.color[1] = color.y;
            g.color[2] = color.z;
            g.flags = flags;
            g.prev_position[0] = drawn_before.x;
            g.prev_position[1] = drawn_before.y;
            g.prev_position[2] = drawn_before.z;
            g.prev_position[3] = 0.0f;
        };

        /* Destruction debris (colours are linear already, converted from sRGB at spawn) */
        for (int32_t i = 0; sys && i < sys->count && outside_count_ + inside_count_ < MAX_PARTICLE_INSTANCES; i++)
        {
            const Particle *p = &sys->particles[i];
            if (p->active)
                add(p->prev_position, p->position, p->radius, p->color, 1.0f);
        }

        /* Environmental particles (weather, ambient) */
        for (int32_t i = 0; env_sys && i < env_sys->count && outside_count_ + inside_count_ < MAX_PARTICLE_INSTANCES; i++)
        {
            const EnvParticle *p = &env_sys->particles[i];
            if (p->active)
                add(p->prev_position, p->position, p->radius, p->color, p->emissive ? 2.0f : 1.0f);
        }

        return outside_count_ + inside_count_;
    }

    void ParticlePass::draw(const FrameContext &frame, ParticlePushConstants pc) const
    {
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame.slot], 0, nullptr);

        /* One proxy cube (36 vertices) per particle */
        const struct
        {
            VkPipeline pipeline;
            uint32_t count;
            uint32_t first;
        } draws[2] = {{pipeline_, outside_count_, 0}, {inside_pipeline_, inside_count_, MAX_PARTICLE_INSTANCES - inside_count_}};
        for (int32_t inside = 0; inside < 2; inside++)
        {
            if (draws[inside].count == 0)
                continue;
            pc.camera_inside = inside;
            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draws[inside].pipeline);
            vkCmdPushConstants(frame.cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc), &pc);
            vkCmdDraw(frame.cmd, 36, draws[inside].count, 0, draws[inside].first);
        }
    }

}
