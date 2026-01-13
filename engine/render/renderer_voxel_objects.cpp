#include "renderer.h"
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
            vkDestroyImage(device_, vobj_atlas_image_, nullptr);
            vobj_atlas_image_ = VK_NULL_HANDLE;
        }

        if (vobj_atlas_memory_)
        {
            vkFreeMemory(device_, vobj_atlas_memory_, nullptr);
            vobj_atlas_memory_ = VK_NULL_HANDLE;
        }

        if (vobj_metadata_buffer_.buffer)
        {
            if (vobj_metadata_mapped_)
            {
                vkUnmapMemory(device_, vobj_metadata_buffer_.memory);
                vobj_metadata_mapped_ = nullptr;
            }
            destroy_buffer(&vobj_metadata_buffer_);
        }

        if (vobj_staging_buffer_.buffer)
        {
            if (vobj_staging_mapped_)
            {
                vkUnmapMemory(device_, vobj_staging_buffer_.memory);
                vobj_staging_mapped_ = nullptr;
            }
            destroy_buffer(&vobj_staging_buffer_);
        }

        vobj_resources_initialized_ = false;
    }

    bool Renderer::create_vobj_atlas_resources(uint32_t max_objects)
    {
        vobj_atlas_image_ = VK_NULL_HANDLE;
        vobj_atlas_memory_ = VK_NULL_HANDLE;
        vobj_atlas_view_ = VK_NULL_HANDLE;
        vobj_atlas_sampler_ = VK_NULL_HANDLE;
        vobj_metadata_buffer_ = {};
        vobj_metadata_mapped_ = nullptr;
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

        if (vkCreateImage(device_, &image_info, nullptr, &vobj_atlas_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create voxel object atlas image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, vobj_atlas_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &vobj_atlas_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate voxel object atlas memory\n");
            return false;
        }

        vkBindImageMemory(device_, vobj_atlas_image_, vobj_atlas_memory_, 0);

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
        create_buffer(metadata_size,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &vobj_metadata_buffer_);

        vkMapMemory(device_, vobj_metadata_buffer_.memory, 0, metadata_size, 0, &vobj_metadata_mapped_);

        VkDeviceSize staging_size = static_cast<VkDeviceSize>(VOBJ_GRID_DIM) * VOBJ_GRID_DIM * VOBJ_GRID_DIM;
        create_buffer(staging_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &vobj_staging_buffer_);

        vkMapMemory(device_, vobj_staging_buffer_.memory, 0, staging_size, 0, &vobj_staging_mapped_);

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
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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

        VkPipelineColorBlendAttachmentState blend_attachments[4]{};
        for (int i = 0; i < 4; i++)
        {
            blend_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend_attachments[i].blendEnable = VK_FALSE;
        }

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.attachmentCount = 4;
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

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vobj_pipeline_);

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
            metadata_info.buffer = vobj_metadata_buffer_.buffer;
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
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue_);

        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
    }

    void Renderer::upload_vobj_metadata(const VoxelObjectWorld *world)
    {
        if (!vobj_resources_initialized_ || !world || !vobj_metadata_mapped_)
            return;

        VoxelObjectGPU *gpu_data = static_cast<VoxelObjectGPU *>(vobj_metadata_mapped_);

        for (int32_t i = 0; i < world->object_count && i < static_cast<int32_t>(vobj_max_objects_); i++)
        {
            const VoxelObject *obj = &world->objects[i];
            VoxelObjectGPU *gpu = &gpu_data[i];

            float vs = obj->voxel_size;
            float half_size = vs * static_cast<float>(VOBJ_GRID_SIZE) * 0.5f;

            float rot_mat[9];
            quat_to_mat3(obj->orientation, rot_mat);

            /* Position is the object center; no center-of-mass offset. */
            const Vec3 translation = obj->position;

            /*
             * IMPORTANT:
             * - quat_to_mat3() returns a row-major 3x3.
             * - GLSL mat4 is column-major.
             * - local_to_world maps grid units -> world (includes voxel_size scale).
             * - world_to_local maps world -> local world-units (no scale), so the shader can
             *   convert to grid units by dividing by voxel_size.
             */

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

            gpu->bounds_min[0] = -half_size;
            gpu->bounds_min[1] = -half_size;
            gpu->bounds_min[2] = -half_size;
            gpu->bounds_min[3] = vs;

            gpu->bounds_max[0] = half_size;
            gpu->bounds_max[1] = half_size;
            gpu->bounds_max[2] = half_size;
            gpu->bounds_max[3] = static_cast<float>(VOBJ_GRID_SIZE);

            gpu->position[0] = obj->position.x;
            gpu->position[1] = obj->position.y;
            gpu->position[2] = obj->position.z;
            gpu->position[3] = obj->active ? 1.0f : 0.0f;

            gpu->atlas_slice = static_cast<uint32_t>(i);
            gpu->material_base = 0;
            gpu->flags = 0;
            gpu->occupancy_mask = static_cast<uint32_t>(obj->occupancy_mask);
        }
    }

    void Renderer::render_voxel_objects_raymarched(const VoxelObjectWorld *world)
    {
        if (!world || world->object_count == 0)
            return;

        if (!vobj_resources_initialized_ || vobj_pipeline_ == VK_NULL_HANDLE)
            return;

        static int32_t prev_object_count = 0;

        /*
         * Ensure GPU atlas stays in sync when voxel grids change due to destruction/splitting.
         * We use voxel_count as a cheap, deterministic proxy for "geometry changed".
         */
        const int32_t max_tracked = static_cast<int32_t>(vobj_max_objects_);
        for (int32_t i = 0; i < world->object_count && i < max_tracked; i++)
        {
            const VoxelObject *obj = &world->objects[i];
            const int32_t current_count = (obj->active ? obj->voxel_count : 0);
            if (vobj_voxel_count_cache_[i] != current_count)
            {
                vobj_voxel_count_cache_[i] = current_count;
                if (obj->active)
                {
                    mark_vobj_dirty(static_cast<uint32_t>(i));
                }
            }
        }

        for (int32_t i = prev_object_count; i < world->object_count && i < static_cast<int32_t>(vobj_max_objects_); i++)
        {
            mark_vobj_dirty(static_cast<uint32_t>(i));
        }
        prev_object_count = world->object_count;

        constexpr int32_t MAX_UPLOADS_PER_FRAME = 8;
        int32_t uploads = 0;
        for (int32_t i = 0; i < world->object_count && i < static_cast<int32_t>(vobj_max_objects_) && uploads < MAX_UPLOADS_PER_FRAME; i++)
        {
            if (is_vobj_dirty(static_cast<uint32_t>(i)) && world->objects[i].active)
            {
                upload_vobj_to_atlas(static_cast<uint32_t>(i), &world->objects[i]);
                clear_vobj_dirty(static_cast<uint32_t>(i));
                uploads++;
            }
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
            int32_t rt_quality;
            int32_t pad2[2];
        } pc;

        Mat4 view_proj = mat4_multiply(projection_matrix_, view_matrix_);
        pc.view_proj = view_proj;
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.pad1 = 0.0f;
        pc.object_count = world->object_count;
        pc.atlas_dim = static_cast<int32_t>(VOBJ_GRID_DIM);
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.debug_mode = terrain_debug_mode_;
        pc.rt_quality = rt_quality_;
        pc.pad2[0] = pc.pad2[1] = 0;

        vkCmdPushConstants(command_buffers_[current_frame_], vobj_pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        uint32_t instance_count = static_cast<uint32_t>(world->object_count);
        if (instance_count > vobj_max_objects_)
            instance_count = vobj_max_objects_;

        vkCmdDraw(command_buffers_[current_frame_], 36, instance_count, 0, 0);
    }

}
