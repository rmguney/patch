#include "renderer.h"
#include "voxel_push_constants.h"
#include "engine/core/profile.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    static const VkFormat GBUFFER_FORMATS[Renderer::GBUFFER_COUNT] = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R32_SFLOAT};

    bool Renderer::create_gbuffer_resources()
    {
        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            gbuffer_images_[i] = VK_NULL_HANDLE;
            gbuffer_memory_[i] = VK_NULL_HANDLE;
            gbuffer_views_[i] = VK_NULL_HANDLE;
        }
        gbuffer_sampler_ = VK_NULL_HANDLE;
        gbuffer_render_pass_ = VK_NULL_HANDLE;
        gbuffer_render_pass_load_ = VK_NULL_HANDLE;
        gbuffer_framebuffer_ = VK_NULL_HANDLE;
        gbuffer_initialized_ = false;

        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.extent.width = swapchain_extent_.width;
            image_info.extent.height = swapchain_extent_.height;
            image_info.extent.depth = 1;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = GBUFFER_FORMATS[i];
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(device_, &image_info, nullptr, &gbuffer_images_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create G-buffer image %u\n", i);
                return false;
            }

            VkMemoryRequirements mem_reqs;
            vkGetImageMemoryRequirements(device_, gbuffer_images_[i], &mem_reqs);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_reqs.size;
            alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device_, &alloc_info, nullptr, &gbuffer_memory_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate G-buffer memory %u\n", i);
                return false;
            }

            vkBindImageMemory(device_, gbuffer_images_[i], gbuffer_memory_[i], 0);

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = gbuffer_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = GBUFFER_FORMATS[i];
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &gbuffer_views_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create G-buffer view %u\n", i);
                return false;
            }
        }

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(device_, &sampler_info, nullptr, &gbuffer_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer sampler\n");
            return false;
        }

        if (!create_gbuffer_render_pass())
        {
            return false;
        }

        VkImageView fb_attachments[6] = {
            gbuffer_views_[GBUFFER_ALBEDO],
            gbuffer_views_[GBUFFER_NORMAL],
            gbuffer_views_[GBUFFER_MATERIAL],
            gbuffer_views_[GBUFFER_LINEAR_DEPTH],
            motion_vector_view_,
            depth_image_view_};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = gbuffer_render_pass_;
        fb_info.attachmentCount = 6;
        fb_info.pAttachments = fb_attachments;
        fb_info.width = swapchain_extent_.width;
        fb_info.height = swapchain_extent_.height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(device_, &fb_info, nullptr, &gbuffer_framebuffer_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer framebuffer\n");
            return false;
        }

        gbuffer_initialized_ = true;
        printf("  G-buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_gbuffer_render_pass()
    {
        VkAttachmentDescription attachments[6]{};

        attachments[0].format = GBUFFER_FORMATS[GBUFFER_ALBEDO];
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachments[1].format = GBUFFER_FORMATS[GBUFFER_NORMAL];
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachments[2].format = GBUFFER_FORMATS[GBUFFER_MATERIAL];
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachments[3].format = GBUFFER_FORMATS[GBUFFER_LINEAR_DEPTH];
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        /* Motion vectors (RG16F) */
        attachments[4].format = VK_FORMAT_R16G16_SFLOAT;
        attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[4].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachments[5].format = VK_FORMAT_D32_SFLOAT;
        attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_refs[5] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};

        VkAttachmentReference depth_ref{5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 5;
        subpass.pColorAttachments = color_refs;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dependencies[2]{};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.attachmentCount = 6;
        rp_info.pAttachments = attachments;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;
        rp_info.dependencyCount = 2;
        rp_info.pDependencies = dependencies;

        if (vkCreateRenderPass(device_, &rp_info, nullptr, &gbuffer_render_pass_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer render pass\n");
            return false;
        }

        /* Create load render pass for post-compute (loads colors, clears depth) */
        for (int i = 0; i < 5; i++)
        {
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateRenderPass(device_, &rp_info, nullptr, &gbuffer_render_pass_load_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer load render pass\n");
            return false;
        }

        /* Create load render pass with depth primed (loads colors AND depth) */
        attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[5].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        if (vkCreateRenderPass(device_, &rp_info, nullptr, &gbuffer_render_pass_load_with_depth_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer load-with-depth render pass\n");
            return false;
        }

        return true;
    }

    void Renderer::destroy_gbuffer_resources()
    {
        if (!gbuffer_initialized_)
            return;

        vkDeviceWaitIdle(device_);

        if (gbuffer_framebuffer_)
        {
            vkDestroyFramebuffer(device_, gbuffer_framebuffer_, nullptr);
            gbuffer_framebuffer_ = VK_NULL_HANDLE;
        }

        if (gbuffer_render_pass_)
        {
            vkDestroyRenderPass(device_, gbuffer_render_pass_, nullptr);
            gbuffer_render_pass_ = VK_NULL_HANDLE;
        }

        if (gbuffer_render_pass_load_)
        {
            vkDestroyRenderPass(device_, gbuffer_render_pass_load_, nullptr);
            gbuffer_render_pass_load_ = VK_NULL_HANDLE;
        }

        if (gbuffer_render_pass_load_with_depth_)
        {
            vkDestroyRenderPass(device_, gbuffer_render_pass_load_with_depth_, nullptr);
            gbuffer_render_pass_load_with_depth_ = VK_NULL_HANDLE;
        }

        if (gbuffer_sampler_)
        {
            vkDestroySampler(device_, gbuffer_sampler_, nullptr);
            gbuffer_sampler_ = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            if (gbuffer_views_[i])
            {
                vkDestroyImageView(device_, gbuffer_views_[i], nullptr);
                gbuffer_views_[i] = VK_NULL_HANDLE;
            }
            if (gbuffer_images_[i])
            {
                vkDestroyImage(device_, gbuffer_images_[i], nullptr);
                gbuffer_images_[i] = VK_NULL_HANDLE;
            }
            if (gbuffer_memory_[i])
            {
                vkFreeMemory(device_, gbuffer_memory_[i], nullptr);
                gbuffer_memory_[i] = VK_NULL_HANDLE;
            }
        }

        if (gbuffer_pipeline_)
        {
            vkDestroyPipeline(device_, gbuffer_pipeline_, nullptr);
            gbuffer_pipeline_ = VK_NULL_HANDLE;
        }

        if (gbuffer_pipeline_layout_)
        {
            vkDestroyPipelineLayout(device_, gbuffer_pipeline_layout_, nullptr);
            gbuffer_pipeline_layout_ = VK_NULL_HANDLE;
        }

        if (gbuffer_descriptor_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_descriptor_layout_, nullptr);
            gbuffer_descriptor_layout_ = VK_NULL_HANDLE;
        }

        if (deferred_lighting_pipeline_)
        {
            vkDestroyPipeline(device_, deferred_lighting_pipeline_, nullptr);
            deferred_lighting_pipeline_ = VK_NULL_HANDLE;
        }

        if (deferred_lighting_layout_)
        {
            vkDestroyPipelineLayout(device_, deferred_lighting_layout_, nullptr);
            deferred_lighting_layout_ = VK_NULL_HANDLE;
        }

        if (deferred_lighting_descriptor_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, deferred_lighting_descriptor_layout_, nullptr);
            deferred_lighting_descriptor_layout_ = VK_NULL_HANDLE;
        }

        destroy_depth_prime_resources();

        gbuffer_initialized_ = false;
    }

    void Renderer::begin_gbuffer_pass()
    {
        if (!gbuffer_initialized_)
            return;

        VkClearValue clear_values[6]{};
        clear_values[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[1].color = {{0.5f, 0.5f, 0.5f, 0.0f}};
        clear_values[2].color = {{1.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[3].color = {{1000.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[4].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[5].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        /* Select render pass based on compute and depth prime state */
        if (!gbuffer_compute_dispatched_)
            rp_info.renderPass = gbuffer_render_pass_;
        else if (depth_primed_this_frame_)
            rp_info.renderPass = gbuffer_render_pass_load_with_depth_;
        else
            rp_info.renderPass = gbuffer_render_pass_load_;
        rp_info.framebuffer = gbuffer_framebuffer_;
        rp_info.renderArea.offset = {0, 0};
        rp_info.renderArea.extent = swapchain_extent_;
        rp_info.clearValueCount = 6;
        rp_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(command_buffers_[current_frame_], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain_extent_.width);
        viewport.height = static_cast<float>(swapchain_extent_.height);
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.extent = swapchain_extent_;

        vkCmdSetViewport(command_buffers_[current_frame_], 0, 1, &viewport);
        vkCmdSetScissor(command_buffers_[current_frame_], 0, 1, &scissor);

        reset_bind_state();
    }

    void Renderer::end_gbuffer_pass()
    {
        if (!gbuffer_initialized_)
            return;

        vkCmdEndRenderPass(command_buffers_[current_frame_]);

        /* Dispatch shadow compute after gbuffer is complete (skip if no chunks to process) */
        if (compute_resources_initialized_ && shadow_compute_pipeline_ && deferred_total_chunks_ > 0)
        {
            PROFILE_BEGIN(PROFILE_RENDER_SHADOW);
            dispatch_shadow_compute();
            dispatch_temporal_shadow_resolve();
            PROFILE_END(PROFILE_RENDER_SHADOW);
        }

        /* Dispatch AO compute after shadow (only if rt_quality >= 1) */
        if (rt_quality_ >= 1 && ao_resources_initialized_ && ao_compute_pipeline_ && deferred_total_chunks_ > 0)
        {
            dispatch_ao_compute();
            dispatch_temporal_ao_resolve();
        }
        gbuffer_compute_dispatched_ = false;
    }

    void Renderer::prepare_gbuffer_compute(const VoxelVolume *vol, const VoxelObjectWorld *objects, bool has_objects_or_particles)
    {
        if (!gbuffer_initialized_ || !vol || !voxel_resources_initialized_)
            return;

        /* Dispatch compute terrain + objects if enabled (must be called before begin_gbuffer_pass) */
        if (compute_raymarching_enabled_ && compute_resources_initialized_ && gbuffer_compute_pipeline_)
        {
            int32_t object_count = (objects && vobj_resources_initialized_) ? objects->object_count : 0;
            dispatch_gbuffer_compute(vol, object_count);

            /* Prime hardware depth buffer only when objects/particles will be rendered */
            depth_primed_this_frame_ = false;
            if (has_objects_or_particles)
            {
                dispatch_depth_prime();
                depth_primed_this_frame_ = true;
            }
        }
    }

    void Renderer::render_gbuffer_terrain(const VoxelVolume *vol)
    {
        if (!gbuffer_initialized_ || !vol || !voxel_resources_initialized_)
            return;

        /* Skip if compute was already dispatched by prepare_gbuffer_compute */
        if (gbuffer_compute_dispatched_)
            return;

        if (!gbuffer_pipeline_)
            return;

        terrain_draw_count_++;

        deferred_bounds_min_[0] = vol->bounds.min_x;
        deferred_bounds_min_[1] = vol->bounds.min_y;
        deferred_bounds_min_[2] = vol->bounds.min_z;
        deferred_bounds_max_[0] = vol->bounds.max_x;
        deferred_bounds_max_[1] = vol->bounds.max_y;
        deferred_bounds_max_[2] = vol->bounds.max_z;
        deferred_voxel_size_ = vol->voxel_size;
        deferred_grid_size_[0] = vol->chunks_x * CHUNK_SIZE;
        deferred_grid_size_[1] = vol->chunks_y * CHUNK_SIZE;
        deferred_grid_size_[2] = vol->chunks_z * CHUNK_SIZE;
        deferred_total_chunks_ = vol->total_chunks;
        deferred_chunks_dim_[0] = vol->chunks_x;
        deferred_chunks_dim_[1] = vol->chunks_y;
        deferred_chunks_dim_[2] = vol->chunks_z;

        vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, gbuffer_pipeline_);
        vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                gbuffer_pipeline_layout_, 0, 1, &gbuffer_descriptor_sets_[current_frame_], 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.bounds_min[0] = vol->bounds.min_x;
        pc.bounds_min[1] = vol->bounds.min_y;
        pc.bounds_min[2] = vol->bounds.min_z;
        pc.voxel_size = vol->voxel_size;
        pc.bounds_max[0] = vol->bounds.max_x;
        pc.bounds_max[1] = vol->bounds.max_y;
        pc.bounds_max[2] = vol->bounds.max_z;
        pc.chunk_size = static_cast<float>(CHUNK_SIZE);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.pad1 = 0.0f;
        pc.grid_size[0] = vol->chunks_x * CHUNK_SIZE;
        pc.grid_size[1] = vol->chunks_y * CHUNK_SIZE;
        pc.grid_size[2] = vol->chunks_z * CHUNK_SIZE;
        pc.total_chunks = vol->total_chunks;
        pc.chunks_dim[0] = vol->chunks_x;
        pc.chunks_dim[1] = vol->chunks_y;
        pc.chunks_dim[2] = vol->chunks_z;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.rt_quality = rt_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = 512;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.object_count = 0;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;

        vkCmdPushConstants(command_buffers_[current_frame_], gbuffer_pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        vkCmdDraw(command_buffers_[current_frame_], 3, 1, 0, 0);
    }

    void Renderer::render_deferred_lighting(uint32_t image_index)
    {
        if (!gbuffer_initialized_ || !deferred_lighting_pipeline_)
            return;

        VkClearValue clear_values[2]{};
        clear_values[0].color = {{0.85f, 0.93f, 1.0f, 1.0f}}; /* Light pastel baby blue */
        clear_values[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_info.renderPass = render_pass_;
        rp_info.framebuffer = framebuffers_[image_index];
        rp_info.renderArea.offset = {0, 0};
        rp_info.renderArea.extent = swapchain_extent_;
        rp_info.clearValueCount = 2;
        rp_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(command_buffers_[current_frame_], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain_extent_.width);
        viewport.height = static_cast<float>(swapchain_extent_.height);
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.extent = swapchain_extent_;

        vkCmdSetViewport(command_buffers_[current_frame_], 0, 1, &viewport);
        vkCmdSetScissor(command_buffers_[current_frame_], 0, 1, &scissor);

        vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, deferred_lighting_pipeline_);
        vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                deferred_lighting_layout_, 0, 1, &deferred_lighting_descriptor_sets_[current_frame_], 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.bounds_min[0] = deferred_bounds_min_[0];
        pc.bounds_min[1] = deferred_bounds_min_[1];
        pc.bounds_min[2] = deferred_bounds_min_[2];
        pc.voxel_size = deferred_voxel_size_;
        pc.bounds_max[0] = deferred_bounds_max_[0];
        pc.bounds_max[1] = deferred_bounds_max_[1];
        pc.bounds_max[2] = deferred_bounds_max_[2];
        pc.chunk_size = static_cast<float>(CHUNK_SIZE);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.pad1 = 0.0f;
        pc.grid_size[0] = deferred_grid_size_[0];
        pc.grid_size[1] = deferred_grid_size_[1];
        pc.grid_size[2] = deferred_grid_size_[2];
        pc.total_chunks = deferred_total_chunks_;
        pc.chunks_dim[0] = deferred_chunks_dim_[0];
        pc.chunks_dim[1] = deferred_chunks_dim_[1];
        pc.chunks_dim[2] = deferred_chunks_dim_[2];
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.rt_quality = rt_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = 512;
        pc.object_count = 0;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;

        vkCmdPushConstants(command_buffers_[current_frame_], deferred_lighting_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        vkCmdDraw(command_buffers_[current_frame_], 3, 1, 0, 0);
    }

    bool Renderer::init_deferred_pipeline()
    {
        printf("Initializing deferred rendering pipeline...\n");

        if (!create_motion_vector_resources())
        {
            fprintf(stderr, "Failed to create motion vector resources\n");
            return false;
        }

        if (!create_gbuffer_resources())
        {
            fprintf(stderr, "Failed to create G-buffer resources\n");
            return false;
        }

        if (!create_gbuffer_pipeline())
        {
            fprintf(stderr, "Failed to create G-buffer pipeline\n");
            return false;
        }

        if (!create_deferred_lighting_pipeline())
        {
            fprintf(stderr, "Failed to create deferred lighting pipeline\n");
            return false;
        }

        if (!create_blue_noise_texture())
        {
            fprintf(stderr, "Failed to create blue noise texture\n");
            return false;
        }

        if (!create_depth_prime_resources())
        {
            fprintf(stderr, "Failed to create depth prime resources\n");
            return false;
        }

        printf("  Deferred pipeline initialized\n");
        return true;
    }

    bool Renderer::init_deferred_descriptors()
    {
        if (!gbuffer_initialized_ || voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return true;

        if (!create_gbuffer_descriptor_sets())
        {
            fprintf(stderr, "Failed to create G-buffer descriptor sets\n");
            return false;
        }

        if (!create_deferred_lighting_descriptor_sets())
        {
            fprintf(stderr, "Failed to create deferred lighting descriptor sets\n");
            return false;
        }

        printf("  Deferred descriptor sets initialized\n");
        return true;
    }

}
