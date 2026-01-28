#include "renderer.h"
#include "gpu_bvh.h"
#include "engine/voxel/bvh.h"
#include "shaders_embedded.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    void Renderer::mark_vobj_dirty(uint32_t index)
    {
        if (index < vobj_max_objects_)
            vobj_dirty_mask_[index / 32] |= (1u << (index % 32));
    }

    bool Renderer::is_vobj_dirty(uint32_t index) const
    {
        if (index >= vobj_max_objects_)
            return false;
        return (vobj_dirty_mask_[index / 32] & (1u << (index % 32))) != 0;
    }

    void Renderer::clear_vobj_dirty(uint32_t index)
    {
        if (index < vobj_max_objects_)
            vobj_dirty_mask_[index / 32] &= ~(1u << (index % 32));
    }

    bool Renderer::init_voxel_object_resources(uint32_t max_objects)
    {
        if (vobj_resources_initialized_)
            return true;

        max_objects = (max_objects > VOBJ_ATLAS_MAX_OBJECTS) ? VOBJ_ATLAS_MAX_OBJECTS : max_objects;
        vobj_max_objects_ = max_objects;

        if (!create_vobj_atlas_resources(max_objects))
        {
            fprintf(stderr, "Failed to create voxel object atlas resources\n");
            return false;
        }

        if (!create_vobj_pipeline())
        {
            fprintf(stderr, "Failed to create voxel object pipeline\n");
            return false;
        }

        if (!create_vobj_descriptor_sets())
        {
            fprintf(stderr, "Failed to create voxel object descriptor sets\n");
            return false;
        }

        memset(vobj_dirty_mask_, 0, sizeof(vobj_dirty_mask_));
        vobj_resources_initialized_ = true;

        /* Update gbuffer compute vobj descriptors with actual vobj resources (for shadow tracing) */
        if (gbuffer_compute_descriptor_pool_ && vobj_atlas_view_ && vobj_atlas_sampler_)
        {
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkDescriptorImageInfo atlas_info{};
                atlas_info.sampler = vobj_atlas_sampler_;
                atlas_info.imageView = vobj_atlas_view_;
                atlas_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkDescriptorBufferInfo vobj_meta_info{};
                vobj_meta_info.buffer = vobj_metadata_buffer_[i].buffer;
                vobj_meta_info.offset = 0;
                vobj_meta_info.range = VK_WHOLE_SIZE;

                VkDescriptorBufferInfo bvh_info{};
                bvh_info.buffer = bvh_buffer_.buffer;
                bvh_info.offset = 0;
                bvh_info.range = sizeof(GPUBVHBuffer);

                VkWriteDescriptorSet vobj_writes[3]{};
                vobj_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                vobj_writes[0].dstSet = gbuffer_compute_vobj_sets_[i];
                vobj_writes[0].dstBinding = 0;
                vobj_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                vobj_writes[0].descriptorCount = 1;
                vobj_writes[0].pImageInfo = &atlas_info;

                vobj_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                vobj_writes[1].dstSet = gbuffer_compute_vobj_sets_[i];
                vobj_writes[1].dstBinding = 1;
                vobj_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                vobj_writes[1].descriptorCount = 1;
                vobj_writes[1].pBufferInfo = &vobj_meta_info;

                vobj_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                vobj_writes[2].dstSet = gbuffer_compute_vobj_sets_[i];
                vobj_writes[2].dstBinding = 2;
                vobj_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                vobj_writes[2].descriptorCount = 1;
                vobj_writes[2].pBufferInfo = &bvh_info;

                vkUpdateDescriptorSets(device_, 3, vobj_writes, 0, nullptr);
            }
        }

        printf("  Voxel object resources initialized: %u max objects\n", max_objects);
        return true;
    }

    void Renderer::destroy_voxel_object_resources()
    {
        if (!vobj_resources_initialized_)
            return;

        vkDeviceWaitIdle(device_);

        if (vobj_pipeline_)
        {
            vkDestroyPipeline(device_, vobj_pipeline_, nullptr);
            vobj_pipeline_ = VK_NULL_HANDLE;
        }

        if (vobj_pipeline_layout_)
        {
            vkDestroyPipelineLayout(device_, vobj_pipeline_layout_, nullptr);
            vobj_pipeline_layout_ = VK_NULL_HANDLE;
        }

        if (vobj_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, vobj_descriptor_pool_, nullptr);
            vobj_descriptor_pool_ = VK_NULL_HANDLE;
        }

        if (vobj_descriptor_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, vobj_descriptor_layout_, nullptr);
            vobj_descriptor_layout_ = VK_NULL_HANDLE;
        }

        if (vobj_atlas_sampler_)
        {
            vkDestroySampler(device_, vobj_atlas_sampler_, nullptr);
            vobj_atlas_sampler_ = VK_NULL_HANDLE;
        }

        if (vobj_atlas_view_)
        {
            vkDestroyImageView(device_, vobj_atlas_view_, nullptr);
            vobj_atlas_view_ = VK_NULL_HANDLE;
        }

        if (vobj_atlas_image_)
        {
            gpu_allocator_.destroy_image(vobj_atlas_image_, vobj_atlas_memory_);
            vobj_atlas_image_ = VK_NULL_HANDLE;
            vobj_atlas_memory_ = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vobj_metadata_buffer_[i].buffer)
            {
                if (vobj_metadata_mapped_[i])
                {
                    gpu_allocator_.unmap(vobj_metadata_buffer_[i].allocation);
                    vobj_metadata_mapped_[i] = nullptr;
                }
                destroy_buffer(&vobj_metadata_buffer_[i]);
            }
        }

        if (vobj_staging_buffer_.buffer)
        {
            if (vobj_staging_mapped_)
            {
                gpu_allocator_.unmap(vobj_staging_buffer_.allocation);
                vobj_staging_mapped_ = nullptr;
            }
            destroy_buffer(&vobj_staging_buffer_);
        }

        if (bvh_buffer_.buffer)
        {
            if (bvh_mapped_)
            {
                gpu_allocator_.unmap(bvh_buffer_.allocation);
                bvh_mapped_ = nullptr;
            }
            destroy_buffer(&bvh_buffer_);
        }

        if (cpu_bvh_)
        {
            bvh_destroy(cpu_bvh_);
            cpu_bvh_ = nullptr;
        }

        vobj_resources_initialized_ = false;
    }

    bool Renderer::create_vobj_atlas_resources(uint32_t max_objects)
    {
        vobj_atlas_image_ = VK_NULL_HANDLE;
        vobj_atlas_memory_ = VK_NULL_HANDLE;
        vobj_atlas_view_ = VK_NULL_HANDLE;
        vobj_atlas_sampler_ = VK_NULL_HANDLE;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vobj_metadata_buffer_[i] = {};
            vobj_metadata_mapped_[i] = nullptr;
        }
        vobj_staging_buffer_ = {};
        vobj_staging_mapped_ = nullptr;

        uint32_t atlas_depth = max_objects * VOBJ_GRID_DIM;

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_3D;
        image_info.extent.width = VOBJ_GRID_DIM;
        image_info.extent.height = VOBJ_GRID_DIM;
        image_info.extent.depth = atlas_depth;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8_UINT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        vobj_atlas_image_ = gpu_allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &vobj_atlas_memory_);
        if (vobj_atlas_image_ == VK_NULL_HANDLE)
        {
            fprintf(stderr, "Failed to create voxel object atlas image\n");
            return false;
        }

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = vobj_atlas_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
        view_info.format = VK_FORMAT_R8_UINT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &vobj_atlas_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object atlas view\n");
            return false;
        }

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(device_, &sampler_info, nullptr, &vobj_atlas_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object atlas sampler\n");
            return false;
        }

        VkDeviceSize metadata_size = static_cast<VkDeviceSize>(max_objects) * sizeof(VoxelObjectGPU);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            create_buffer(metadata_size,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vobj_metadata_buffer_[i]);

            vobj_metadata_mapped_[i] = gpu_allocator_.map(vobj_metadata_buffer_[i].allocation);
        }

        constexpr int32_t VOBJ_MAX_UPLOADS_PER_FRAME = 8;
        VkDeviceSize staging_size = static_cast<VkDeviceSize>(VOBJ_GRID_DIM) * VOBJ_GRID_DIM * VOBJ_GRID_DIM * VOBJ_MAX_UPLOADS_PER_FRAME;
        create_buffer(staging_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &vobj_staging_buffer_);

        vobj_staging_mapped_ = gpu_allocator_.map(vobj_staging_buffer_.allocation);

        VkDeviceSize bvh_buffer_size = sizeof(GPUBVHBuffer);
        create_buffer(bvh_buffer_size,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &bvh_buffer_);

        bvh_mapped_ = gpu_allocator_.map(bvh_buffer_.allocation);
        memset(&bvh_data_, 0, sizeof(bvh_data_));
        memcpy(bvh_mapped_, &bvh_data_, sizeof(GPUBVHBuffer));

        cpu_bvh_ = bvh_create();

        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandPool = command_pool_;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &cmd_alloc, &cmd);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vobj_atlas_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue_);

        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);

        printf("  Voxel object atlas created: %ux%ux%u (%.1f MB)\n",
               VOBJ_GRID_DIM, VOBJ_GRID_DIM, atlas_depth,
               static_cast<float>(VOBJ_GRID_DIM * VOBJ_GRID_DIM * atlas_depth) / (1024.0f * 1024.0f));

        return true;
    }

    bool Renderer::create_vobj_pipeline()
    {
        VkDescriptorSetLayoutBinding bindings[3]{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 3;
        layout_info.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &vobj_descriptor_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object descriptor layout\n");
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = 128;

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &vobj_descriptor_layout_;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &vobj_pipeline_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object pipeline layout\n");
            return false;
        }

        VkShaderModuleCreateInfo vert_info{};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = shaders::k_shader_voxel_object_vert_spv_size;
        vert_info.pCode = shaders::k_shader_voxel_object_vert_spv;

        VkShaderModule vert_module;
        if (vkCreateShaderModule(device_, &vert_info, nullptr, &vert_module) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object vertex shader\n");
            return false;
        }

        VkShaderModuleCreateInfo frag_info{};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = shaders::k_shader_voxel_object_frag_spv_size;
        frag_info.pCode = shaders::k_shader_voxel_object_frag_spv;

        VkShaderModule frag_module;
        if (vkCreateShaderModule(device_, &frag_info, nullptr, &frag_module) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device_, vert_module, nullptr);
            fprintf(stderr, "Failed to create voxel object fragment shader\n");
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert_module;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag_module;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain_extent_.width);
        viewport.height = static_cast<float>(swapchain_extent_.height);
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.extent = swapchain_extent_;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend_attachments[5]{};
        for (int i = 0; i < 5; i++)
        {
            blend_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend_attachments[i].blendEnable = VK_FALSE;
        }

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.attachmentCount = 5;
        color_blending.pAttachments = blend_attachments;

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = vobj_pipeline_layout_;
        pipeline_info.renderPass = gbuffer_render_pass_;
        pipeline_info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, pipeline_cache_, 1, &pipeline_info, nullptr, &vobj_pipeline_);

        vkDestroyShaderModule(device_, frag_module, nullptr);
        vkDestroyShaderModule(device_, vert_module, nullptr);

        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object pipeline\n");
            return false;
        }

        return true;
    }

    bool Renderer::create_vobj_descriptor_sets()
    {
        if (voxel_material_buffer_.buffer == VK_NULL_HANDLE)
        {
            fprintf(stderr, "Cannot create vobj descriptor sets: material buffer not initialized\n");
            return false;
        }

        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &vobj_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            layouts[i] = vobj_descriptor_layout_;

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = vobj_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        alloc_info.pSetLayouts = layouts;

        if (vkAllocateDescriptorSets(device_, &alloc_info, vobj_descriptor_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate voxel object descriptor sets\n");
            return false;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo atlas_info{};
            atlas_info.sampler = vobj_atlas_sampler_;
            atlas_info.imageView = vobj_atlas_view_;
            atlas_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorBufferInfo metadata_info{};
            metadata_info.buffer = vobj_metadata_buffer_[i].buffer;
            metadata_info.offset = 0;
            metadata_info.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo material_info{};
            material_info.buffer = voxel_material_buffer_.buffer;
            material_info.offset = 0;
            material_info.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet writes[3]{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = vobj_descriptor_sets_[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &atlas_info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = vobj_descriptor_sets_[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &metadata_info;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = vobj_descriptor_sets_[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &material_info;

            vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
        }

        return true;
    }

    void Renderer::upload_vobj_to_atlas(uint32_t object_index, const VoxelObject *obj)
    {
        if (!vobj_resources_initialized_ || object_index >= vobj_max_objects_)
            return;

        uint8_t *dst = static_cast<uint8_t *>(vobj_staging_mapped_);
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            dst[i] = obj->voxels[i].material;
        }

        VkCommandBuffer cmd = vobj_upload_cmd_[current_frame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vobj_atlas_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, static_cast<int32_t>(object_index * VOBJ_GRID_DIM)};
        region.imageExtent = {VOBJ_GRID_DIM, VOBJ_GRID_DIM, VOBJ_GRID_DIM};

        vkCmdCopyBufferToImage(cmd, vobj_staging_buffer_.buffer, vobj_atlas_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue_);
    }

    void Renderer::upload_vobj_metadata(const VoxelObjectWorld *world)
    {
        if (!vobj_resources_initialized_ || !world || !vobj_metadata_mapped_[current_frame_])
        {
            vobj_visible_count_ = 0;
            vobj_total_count_ = 0;
            return;
        }

        VoxelObjectGPU *gpu_data = static_cast<VoxelObjectGPU *>(vobj_metadata_mapped_[current_frame_]);
        const int32_t count = (world->object_count < static_cast<int32_t>(vobj_max_objects_))
                                  ? world->object_count
                                  : static_cast<int32_t>(vobj_max_objects_);

        /* Extract view frustum for culling */
        Mat4 view_proj = mat4_multiply(projection_matrix_, view_matrix_);
        Frustum frustum = frustum_from_view_proj(view_proj);

        /*
         * Upload ALL objects at their original indices for BVH/compute shader access.
         * BVH traversal returns original object indices, so the metadata buffer must
         * contain all objects at their original positions. Inactive objects are marked
         * with position.w = 0.0 so shaders can skip them.
         */
        int32_t visible_count = 0;
        for (int32_t i = 0; i < count; i++)
        {
            const VoxelObject *obj = &world->objects[i];
            VoxelObjectGPU *gpu = &gpu_data[i];

            float vs = obj->voxel_size;

            /* Check if object should be rendered (for visible count tracking) */
            float max_half_ext = (std::max)(obj->shape_half_extents.x,
                                           (std::max)(obj->shape_half_extents.y,
                                                      obj->shape_half_extents.z));
            float bounding_radius = max_half_ext * 1.732051f;
            FrustumResult cull_result = frustum_test_sphere(&frustum, obj->position, bounding_radius);
            bool atlas_ready = !is_vobj_dirty(static_cast<uint32_t>(i));
            bool render_ready = obj->render_delay <= 0;
            bool visible = (cull_result != FRUSTUM_OUTSIDE) && obj->active && atlas_ready && render_ready;

            if (visible)
                visible_count++;

            /* Always upload at original index for BVH access, but mark inactive with position.w = 0 */
            if (!obj->active)
            {
                memset(gpu, 0, sizeof(VoxelObjectGPU));
                gpu->position[3] = 0.0f; /* Mark as inactive */
                continue;
            }

            float rot_mat[9];
            quat_to_mat3(obj->orientation, rot_mat);

            /* Position is the object center; no center-of-mass offset. */
            const Vec3 translation = obj->position;

            /* Column-major local_to_world = [R * voxel_size, pos] */
            float world_mat[16] = {
                rot_mat[0] * vs, rot_mat[3] * vs, rot_mat[6] * vs, 0.0f,
                rot_mat[1] * vs, rot_mat[4] * vs, rot_mat[7] * vs, 0.0f,
                rot_mat[2] * vs, rot_mat[5] * vs, rot_mat[8] * vs, 0.0f,
                translation.x, translation.y, translation.z, 1.0f};

            /* Translation for world_to_local: t = -R^T * pos (row-major R; use columns of R) */
            Vec3 local_origin = {
                -(translation.x * rot_mat[0] + translation.y * rot_mat[3] + translation.z * rot_mat[6]),
                -(translation.x * rot_mat[1] + translation.y * rot_mat[4] + translation.z * rot_mat[7]),
                -(translation.x * rot_mat[2] + translation.y * rot_mat[5] + translation.z * rot_mat[8])};

            /* Column-major world_to_local = [R^T, -R^T*pos] (no voxel_size scaling) */
            float inv_mat[16] = {
                rot_mat[0], rot_mat[1], rot_mat[2], 0.0f,
                rot_mat[3], rot_mat[4], rot_mat[5], 0.0f,
                rot_mat[6], rot_mat[7], rot_mat[8], 0.0f,
                local_origin.x, local_origin.y, local_origin.z, 1.0f};

            memcpy(gpu->local_to_world, world_mat, sizeof(world_mat));
            memcpy(gpu->world_to_local, inv_mat, sizeof(inv_mat));

            gpu->bounds_min[0] = -obj->shape_half_extents.x;
            gpu->bounds_min[1] = -obj->shape_half_extents.y;
            gpu->bounds_min[2] = -obj->shape_half_extents.z;
            gpu->bounds_min[3] = vs;

            gpu->bounds_max[0] = obj->shape_half_extents.x;
            gpu->bounds_max[1] = obj->shape_half_extents.y;
            gpu->bounds_max[2] = obj->shape_half_extents.z;
            gpu->bounds_max[3] = static_cast<float>(VOBJ_GRID_SIZE);

            gpu->position[0] = obj->position.x;
            gpu->position[1] = obj->position.y;
            gpu->position[2] = obj->position.z;
            /* Only mark as active if atlas data is ready - prevents flickering during spawn */
            bool atlas_ready_for_gpu = !is_vobj_dirty(static_cast<uint32_t>(i));
            gpu->position[3] = (obj->active && atlas_ready_for_gpu) ? 1.0f : 0.0f;

            /* atlas_slice is same as object index since we upload at original indices */
            gpu->atlas_slice = static_cast<uint32_t>(i);
            gpu->material_base = 0;
            gpu->flags = 0;
            gpu->occupancy_mask = static_cast<uint32_t>(obj->occupancy_mask);
        }

        vobj_visible_count_ = visible_count;
        vobj_total_count_ = count;

        if (cpu_bvh_ && bvh_buffer_.buffer && bvh_mapped_)
        {
            if (bvh_needs_rebuild(cpu_bvh_, world))
            {
                bvh_build(cpu_bvh_, world);
            }
            else
            {
                bvh_refit(cpu_bvh_, world);
            }

            bvh_data_.params.node_count = cpu_bvh_->node_count;
            bvh_data_.params.object_count = cpu_bvh_->object_count;
            bvh_data_.params.root_index = 0;
            bvh_data_.params._pad0 = 0;
            bvh_data_.params.scene_bounds_min[0] = world->bounds.min_x;
            bvh_data_.params.scene_bounds_min[1] = world->bounds.min_y;
            bvh_data_.params.scene_bounds_min[2] = world->bounds.min_z;
            bvh_data_.params.scene_bounds_min[3] = 0.0f;
            bvh_data_.params.scene_bounds_max[0] = world->bounds.max_x;
            bvh_data_.params.scene_bounds_max[1] = world->bounds.max_y;
            bvh_data_.params.scene_bounds_max[2] = world->bounds.max_z;
            bvh_data_.params.scene_bounds_max[3] = 0.0f;

            memcpy(bvh_data_.nodes, cpu_bvh_->nodes, sizeof(GPUBVHNode) * cpu_bvh_->node_count);
            memcpy(bvh_data_.object_indices, cpu_bvh_->object_indices, sizeof(int32_t) * cpu_bvh_->object_count);

            memcpy(bvh_mapped_, &bvh_data_, sizeof(GPUBVHBuffer));
        }
    }

    void Renderer::render_voxel_objects_raymarched(const VoxelObjectWorld *world)
    {
        if (!world || world->object_count == 0)
            return;

        if (!vobj_resources_initialized_ || vobj_pipeline_ == VK_NULL_HANDLE)
            return;

        if (world != vobj_last_world_)
        {
            vobj_last_world_ = world;
            vobj_prev_object_count_ = 0;
            memset(vobj_dirty_mask_, 0, sizeof(vobj_dirty_mask_));
            for (uint32_t i = 0; i < vobj_max_objects_; i++)
            {
                vobj_revision_cache_[i] = 0;
            }
        }

        /*
         * Ensure GPU atlas stays in sync when voxel grids change due to destruction/splitting.
         * We use voxel_count as a cheap, deterministic proxy for "geometry changed".
         */
        const int32_t max_tracked = static_cast<int32_t>(vobj_max_objects_);
        for (int32_t i = 0; i < world->object_count && i < max_tracked; i++)
        {
            const VoxelObject *obj = &world->objects[i];
            const uint32_t current_revision = obj->active ? obj->voxel_revision : 0u;
            if (vobj_revision_cache_[i] != current_revision)
            {
                vobj_revision_cache_[i] = current_revision;
                if (obj->active)
                {
                    mark_vobj_dirty(static_cast<uint32_t>(i));
                }
            }
        }

        for (int32_t i = vobj_prev_object_count_; i < world->object_count && i < static_cast<int32_t>(vobj_max_objects_); i++)
        {
            mark_vobj_dirty(static_cast<uint32_t>(i));
        }
        vobj_prev_object_count_ = world->object_count;

        constexpr int32_t MAX_UPLOADS_PER_FRAME = 8;

        if (vobj_upload_pending_[current_frame_] > 0 && upload_timeline_semaphore_)
        {
            VkSemaphoreWaitInfo wait_info{};
            wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.semaphoreCount = 1;
            wait_info.pSemaphores = &upload_timeline_semaphore_;
            wait_info.pValues = &vobj_upload_pending_[current_frame_];
            vkWaitSemaphores(device_, &wait_info, UINT64_MAX);
            vobj_upload_pending_[current_frame_] = 0;
        }

        uint32_t dirty_objects[MAX_UPLOADS_PER_FRAME];
        int32_t dirty_count = 0;
        for (int32_t i = 0; i < world->object_count && i < static_cast<int32_t>(vobj_max_objects_) && dirty_count < MAX_UPLOADS_PER_FRAME; i++)
        {
            if (is_vobj_dirty(static_cast<uint32_t>(i)) && world->objects[i].active)
            {
                dirty_objects[dirty_count++] = static_cast<uint32_t>(i);
            }
        }

        if (dirty_count > 0)
        {
            uint8_t *staging = static_cast<uint8_t *>(vobj_staging_mapped_);
            for (int32_t slot = 0; slot < dirty_count; slot++)
            {
                uint32_t obj_idx = dirty_objects[slot];
                const VoxelObject *obj = &world->objects[obj_idx];
                uint8_t *dst = staging + slot * VOBJ_TOTAL_VOXELS;
                for (int32_t v = 0; v < VOBJ_TOTAL_VOXELS; v++)
                    dst[v] = obj->voxels[v].material;
                clear_vobj_dirty(obj_idx);
            }

            VkCommandBuffer cmd = vobj_upload_cmd_[current_frame_];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_info);

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = vobj_atlas_image_;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            for (int32_t slot = 0; slot < dirty_count; slot++)
            {
                VkBufferImageCopy region{};
                region.bufferOffset = static_cast<VkDeviceSize>(slot) * VOBJ_TOTAL_VOXELS;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, static_cast<int32_t>(dirty_objects[slot] * VOBJ_GRID_DIM)};
                region.imageExtent = {VOBJ_GRID_DIM, VOBJ_GRID_DIM, VOBJ_GRID_DIM};
                vkCmdCopyBufferToImage(cmd, vobj_staging_buffer_.buffer, vobj_atlas_image_,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            vkEndCommandBuffer(cmd);

            uint64_t signal_value = ++upload_timeline_value_;
            VkTimelineSemaphoreSubmitInfo timeline_submit{};
            timeline_submit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            timeline_submit.signalSemaphoreValueCount = 1;
            timeline_submit.pSignalSemaphoreValues = &signal_value;

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.pNext = &timeline_submit;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &upload_timeline_semaphore_;
            vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);

            vobj_upload_pending_[current_frame_] = signal_value;
        }

        upload_vobj_metadata(world);

        vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, vobj_pipeline_);
        vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vobj_pipeline_layout_, 0, 1, &vobj_descriptor_sets_[current_frame_], 0, nullptr);

        struct alignas(16) VoxelObjectPushConstants
        {
            Mat4 view_proj;
            float camera_pos[3];
            float pad1;
            int32_t object_count;
            int32_t atlas_dim;
            float near_plane;
            float far_plane;
            int32_t debug_mode;
            int32_t lod_quality;
            int32_t is_orthographic;
            float camera_forward[3];
        } pc;

        Mat4 view_proj = mat4_multiply(projection_matrix_, view_matrix_);
        pc.view_proj = view_proj;
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.pad1 = 0.0f;
        pc.object_count = vobj_total_count_;
        pc.atlas_dim = static_cast<int32_t>(VOBJ_GRID_DIM);
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.debug_mode = terrain_debug_mode_;
        pc.lod_quality = lod_quality_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.camera_forward[0] = -view_matrix_.m[2];
        pc.camera_forward[1] = -view_matrix_.m[6];
        pc.camera_forward[2] = -view_matrix_.m[10];

        vkCmdPushConstants(command_buffers_[current_frame_], vobj_pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        /* Draw ALL objects by total count - vertex shader culls inactive via position.w.
         * Using visible_count would skip objects at higher indices since metadata is not compacted. */
        uint32_t instance_count = static_cast<uint32_t>(vobj_total_count_);
        if (instance_count > vobj_max_objects_)
            instance_count = vobj_max_objects_;

        vkCmdDraw(command_buffers_[current_frame_], 36, instance_count, 0, 0);
    }

}
