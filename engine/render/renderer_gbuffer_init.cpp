#include "renderer.h"
#include "voxel_push_constants.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    bool Renderer::create_gbuffer_pipeline()
    {
        VkDescriptorSetLayoutBinding bindings[5]{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 5;
        layout_info.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &gbuffer_descriptor_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer descriptor layout\n");
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = 256;

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &gbuffer_descriptor_layout_;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &gbuffer_pipeline_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer pipeline layout\n");
            return false;
        }

        VkShaderModuleCreateInfo vert_info{};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = shaders::k_shader_voxel_vert_spv_size;
        vert_info.pCode = shaders::k_shader_voxel_vert_spv;

        VkShaderModule vert_module;
        if (vkCreateShaderModule(device_, &vert_info, nullptr, &vert_module) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer vertex shader\n");
            return false;
        }

        VkShaderModuleCreateInfo frag_info{};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = shaders::k_shader_gbuffer_terrain_frag_spv_size;
        frag_info.pCode = shaders::k_shader_gbuffer_terrain_frag_spv;

        VkShaderModule frag_module;
        if (vkCreateShaderModule(device_, &frag_info, nullptr, &frag_module) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device_, vert_module, nullptr);
            fprintf(stderr, "Failed to create G-buffer fragment shader\n");
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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
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
        pipeline_info.layout = gbuffer_pipeline_layout_;
        pipeline_info.renderPass = gbuffer_render_pass_;
        pipeline_info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &gbuffer_pipeline_);

        vkDestroyShaderModule(device_, frag_module, nullptr);
        vkDestroyShaderModule(device_, vert_module, nullptr);

        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer pipeline\n");
            return false;
        }

        return true;
    }

    bool Renderer::create_deferred_lighting_pipeline()
    {
        VkDescriptorSetLayoutBinding bindings[6]{};

        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        bindings[4].binding = 5; /* shadow_buffer from compute shadow pass */
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[5].binding = 6; /* blue_noise_tex */
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 6;
        layout_info.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &deferred_lighting_descriptor_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create deferred lighting descriptor layout\n");
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = 256;

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &deferred_lighting_descriptor_layout_;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &deferred_lighting_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create deferred lighting pipeline layout\n");
            return false;
        }

        VkShaderModuleCreateInfo vert_info{};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = shaders::k_shader_voxel_vert_spv_size;
        vert_info.pCode = shaders::k_shader_voxel_vert_spv;

        VkShaderModule vert_module;
        if (vkCreateShaderModule(device_, &vert_info, nullptr, &vert_module) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create deferred lighting vertex shader\n");
            return false;
        }

        VkShaderModuleCreateInfo frag_info{};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = shaders::k_shader_deferred_lighting_frag_spv_size;
        frag_info.pCode = shaders::k_shader_deferred_lighting_frag_spv;

        VkShaderModule frag_module;
        if (vkCreateShaderModule(device_, &frag_info, nullptr, &frag_module) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device_, vert_module, nullptr);
            fprintf(stderr, "Failed to create deferred lighting fragment shader\n");
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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &blend_attachment;

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
        pipeline_info.layout = deferred_lighting_layout_;
        pipeline_info.renderPass = render_pass_;
        pipeline_info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &deferred_lighting_pipeline_);

        vkDestroyShaderModule(device_, frag_module, nullptr);
        vkDestroyShaderModule(device_, vert_module, nullptr);

        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create deferred lighting pipeline\n");
            return false;
        }

        return true;
    }

    bool Renderer::create_gbuffer_descriptor_sets()
    {
        if (voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return true;

        VkDescriptorPoolSize pool_sizes[4]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &gbuffer_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            layouts[i] = gbuffer_descriptor_layout_;

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = gbuffer_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        alloc_info.pSetLayouts = layouts;

        if (vkAllocateDescriptorSets(device_, &alloc_info, gbuffer_descriptor_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate G-buffer descriptor sets\n");
            return false;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo voxel_data_info{};
            voxel_data_info.buffer = voxel_data_buffer_.buffer;
            voxel_data_info.offset = 0;
            voxel_data_info.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo headers_info{};
            headers_info.buffer = voxel_headers_buffer_.buffer;
            headers_info.offset = 0;
            headers_info.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo material_info{};
            material_info.buffer = voxel_material_buffer_.buffer;
            material_info.offset = 0;
            material_info.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo temporal_info{};
            temporal_info.buffer = voxel_temporal_ubo_[i].buffer;
            temporal_info.offset = 0;
            temporal_info.range = sizeof(Mat4);

            VkDescriptorImageInfo depth_info{};
            depth_info.sampler = depth_sampler_;
            depth_info.imageView = depth_image_view_;
            depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[5]{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = gbuffer_descriptor_sets_[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &voxel_data_info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = gbuffer_descriptor_sets_[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &headers_info;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = gbuffer_descriptor_sets_[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &material_info;

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = gbuffer_descriptor_sets_[i];
            writes[3].dstBinding = 3;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &temporal_info;

            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = gbuffer_descriptor_sets_[i];
            writes[4].dstBinding = 4;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[4].descriptorCount = 1;
            writes[4].pImageInfo = &depth_info;

            vkUpdateDescriptorSets(device_, 5, writes, 0, nullptr);
        }

        return true;
    }

    bool Renderer::create_deferred_lighting_descriptor_sets()
    {
        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 7;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &deferred_lighting_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create deferred lighting descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            layouts[i] = deferred_lighting_descriptor_layout_;

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = deferred_lighting_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        alloc_info.pSetLayouts = layouts;

        if (vkAllocateDescriptorSets(device_, &alloc_info, deferred_lighting_descriptor_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate deferred lighting descriptor sets\n");
            return false;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo gbuffer_infos[GBUFFER_COUNT];
            for (uint32_t g = 0; g < GBUFFER_COUNT; g++)
            {
                gbuffer_infos[g].sampler = gbuffer_sampler_;
                gbuffer_infos[g].imageView = gbuffer_views_[g];
                gbuffer_infos[g].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkDescriptorImageInfo shadow_buffer_info{};
            shadow_buffer_info.sampler = gbuffer_sampler_;
            shadow_buffer_info.imageView = shadow_output_view_ ? shadow_output_view_ : gbuffer_views_[0];
            shadow_buffer_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo blue_noise_info{};
            blue_noise_info.sampler = blue_noise_sampler_ ? blue_noise_sampler_ : gbuffer_sampler_;
            blue_noise_info.imageView = blue_noise_view_ ? blue_noise_view_ : gbuffer_views_[0];
            blue_noise_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[6]{};

            for (uint32_t g = 0; g < GBUFFER_COUNT; g++)
            {
                writes[g].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[g].dstSet = deferred_lighting_descriptor_sets_[i];
                writes[g].dstBinding = g;
                writes[g].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[g].descriptorCount = 1;
                writes[g].pImageInfo = &gbuffer_infos[g];
            }

            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = deferred_lighting_descriptor_sets_[i];
            writes[4].dstBinding = 5; /* shadow_buffer from compute shadow pass */
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[4].descriptorCount = 1;
            writes[4].pImageInfo = &shadow_buffer_info;

            writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5].dstSet = deferred_lighting_descriptor_sets_[i];
            writes[5].dstBinding = 6;
            writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[5].descriptorCount = 1;
            writes[5].pImageInfo = &blue_noise_info;

            vkUpdateDescriptorSets(device_, 6, writes, 0, nullptr);
        }

        return true;
    }

    void Renderer::update_deferred_shadow_buffer_descriptor(uint32_t frame_index, VkImageView shadow_view)
    {
        if (!gbuffer_initialized_ || !deferred_lighting_descriptor_pool_ || frame_index >= MAX_FRAMES_IN_FLIGHT)
            return;

        VkDescriptorImageInfo shadow_buffer_info{};
        shadow_buffer_info.sampler = gbuffer_sampler_;
        shadow_buffer_info.imageView = shadow_view ? shadow_view : (shadow_output_view_ ? shadow_output_view_ : gbuffer_views_[0]);
        shadow_buffer_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = deferred_lighting_descriptor_sets_[frame_index];
        write.dstBinding = 5; /* shadow buffer */
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &shadow_buffer_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    bool Renderer::create_depth_prime_resources()
    {
        if (depth_prime_initialized_)
            return true;

        /* Create render pass with only depth attachment */
        VkAttachmentDescription depth_attach{};
        depth_attach.format = VK_FORMAT_D32_SFLOAT;
        depth_attach.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; /* We'll overwrite everything */
        depth_attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attach.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.attachmentCount = 1;
        rp_info.pAttachments = &depth_attach;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;
        rp_info.dependencyCount = 1;
        rp_info.pDependencies = &dep;

        if (vkCreateRenderPass(device_, &rp_info, nullptr, &depth_prime_render_pass_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime render pass\n");
            return false;
        }

        /* Create framebuffer with only depth attachment */
        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = depth_prime_render_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &depth_image_view_;
        fb_info.width = swapchain_extent_.width;
        fb_info.height = swapchain_extent_.height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(device_, &fb_info, nullptr, &depth_prime_framebuffer_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime framebuffer\n");
            return false;
        }

        /* Create descriptor set layout for linear_depth sampler */
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &depth_prime_descriptor_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime descriptor layout\n");
            return false;
        }

        /* Create push constant range */
        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = 8; /* near_plane (4) + far_plane (4) */

        /* Create pipeline layout */
        VkPipelineLayoutCreateInfo pipe_layout_info{};
        pipe_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipe_layout_info.setLayoutCount = 1;
        pipe_layout_info.pSetLayouts = &depth_prime_descriptor_layout_;
        pipe_layout_info.pushConstantRangeCount = 1;
        pipe_layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &pipe_layout_info, nullptr, &depth_prime_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime pipeline layout\n");
            return false;
        }

        /* Create pipeline */
        VkShaderModuleCreateInfo vert_info{};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = shaders::k_shader_voxel_vert_spv_size;
        vert_info.pCode = shaders::k_shader_voxel_vert_spv;

        VkShaderModule vert_module;
        if (vkCreateShaderModule(device_, &vert_info, nullptr, &vert_module) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime vertex shader\n");
            return false;
        }

        VkShaderModuleCreateInfo frag_info{};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = shaders::k_shader_depth_prime_frag_spv_size;
        frag_info.pCode = shaders::k_shader_depth_prime_frag_spv;

        VkShaderModule frag_module;
        if (vkCreateShaderModule(device_, &frag_info, nullptr, &frag_module) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device_, vert_module, nullptr);
            fprintf(stderr, "Failed to create depth prime fragment shader\n");
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

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS; /* Always write */

        VkPipelineColorBlendStateCreateInfo color_blend{};
        color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.attachmentCount = 0; /* No color attachments */

        VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;

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
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = depth_prime_layout_;
        pipeline_info.renderPass = depth_prime_render_pass_;
        pipeline_info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &depth_prime_pipeline_);

        vkDestroyShaderModule(device_, vert_module, nullptr);
        vkDestroyShaderModule(device_, frag_module, nullptr);

        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime pipeline\n");
            return false;
        }

        /* Create descriptor pool and sets */
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &depth_prime_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create depth prime descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            layouts[i] = depth_prime_descriptor_layout_;

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = depth_prime_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        alloc_info.pSetLayouts = layouts;

        if (vkAllocateDescriptorSets(device_, &alloc_info, depth_prime_descriptor_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate depth prime descriptor sets\n");
            return false;
        }

        /* Update descriptor sets to point to linear_depth texture */
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo image_info{};
            image_info.sampler = gbuffer_sampler_;
            image_info.imageView = gbuffer_views_[GBUFFER_LINEAR_DEPTH];
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = depth_prime_descriptor_sets_[i];
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &image_info;

            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }

        depth_prime_initialized_ = true;
        printf("  Depth prime resources initialized\n");
        return true;
    }

    void Renderer::destroy_depth_prime_resources()
    {
        if (!depth_prime_initialized_)
            return;

        vkDeviceWaitIdle(device_);

        if (depth_prime_pipeline_)
        {
            vkDestroyPipeline(device_, depth_prime_pipeline_, nullptr);
            depth_prime_pipeline_ = VK_NULL_HANDLE;
        }
        if (depth_prime_layout_)
        {
            vkDestroyPipelineLayout(device_, depth_prime_layout_, nullptr);
            depth_prime_layout_ = VK_NULL_HANDLE;
        }
        if (depth_prime_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, depth_prime_descriptor_pool_, nullptr);
            depth_prime_descriptor_pool_ = VK_NULL_HANDLE;
        }
        if (depth_prime_descriptor_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, depth_prime_descriptor_layout_, nullptr);
            depth_prime_descriptor_layout_ = VK_NULL_HANDLE;
        }
        if (depth_prime_framebuffer_)
        {
            vkDestroyFramebuffer(device_, depth_prime_framebuffer_, nullptr);
            depth_prime_framebuffer_ = VK_NULL_HANDLE;
        }
        if (depth_prime_render_pass_)
        {
            vkDestroyRenderPass(device_, depth_prime_render_pass_, nullptr);
            depth_prime_render_pass_ = VK_NULL_HANDLE;
        }

        depth_prime_initialized_ = false;
    }

    void Renderer::dispatch_depth_prime()
    {
        if (!depth_prime_initialized_ || !gbuffer_compute_dispatched_)
            return;

        /* Transition linear_depth from color attachment (post-compute) to shader read */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = gbuffer_images_[GBUFFER_LINEAR_DEPTH];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            command_buffers_[current_frame_],
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Begin depth prime render pass */
        VkRenderPassBeginInfo rp_info{};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_info.renderPass = depth_prime_render_pass_;
        rp_info.framebuffer = depth_prime_framebuffer_;
        rp_info.renderArea.offset = {0, 0};
        rp_info.renderArea.extent = swapchain_extent_;

        vkCmdBeginRenderPass(command_buffers_[current_frame_], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain_extent_.width);
        viewport.height = static_cast<float>(swapchain_extent_.height);
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.extent = swapchain_extent_;

        vkCmdSetViewport(command_buffers_[current_frame_], 0, 1, &viewport);
        vkCmdSetScissor(command_buffers_[current_frame_], 0, 1, &scissor);

        vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, depth_prime_pipeline_);
        vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                depth_prime_layout_, 0, 1, &depth_prime_descriptor_sets_[current_frame_], 0, nullptr);

        /* Push near/far planes - must match hardcoded values used elsewhere */
        struct
        {
            float near_plane;
            float far_plane;
        } pc = {0.1f, 1000.0f};

        vkCmdPushConstants(command_buffers_[current_frame_], depth_prime_layout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        /* Draw fullscreen triangle */
        vkCmdDraw(command_buffers_[current_frame_], 3, 1, 0, 0);

        vkCmdEndRenderPass(command_buffers_[current_frame_]);

        /* Transition linear_depth back to color attachment for the G-buffer load pass */
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            command_buffers_[current_frame_],
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

}
