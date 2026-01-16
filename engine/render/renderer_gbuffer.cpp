#include "renderer.h"
#include "voxel_push_constants.h"
#include "shaders_embedded.h"
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

        /* Create load render pass for post-compute (preserves compute results) */
        for (int i = 0; i < 5; i++)
        {
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; /* Still clear depth for voxel objects */
        attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateRenderPass(device_, &rp_info, nullptr, &gbuffer_render_pass_load_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create G-buffer load render pass\n");
            return false;
        }

        return true;
    }

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

        gbuffer_initialized_ = false;
    }

    bool Renderer::create_shadow_volume_resources(uint32_t width, uint32_t height, uint32_t depth)
    {
        shadow_volume_dims_[0] = width;
        shadow_volume_dims_[1] = height;
        shadow_volume_dims_[2] = depth;

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_3D;
        image_info.extent.width = width;
        image_info.extent.height = height;
        image_info.extent.depth = depth;
        image_info.mipLevels = 3;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8_UINT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &shadow_volume_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow volume image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, shadow_volume_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &shadow_volume_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate shadow volume memory\n");
            return false;
        }

        vkBindImageMemory(device_, shadow_volume_image_, shadow_volume_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = shadow_volume_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
        view_info.format = VK_FORMAT_R8_UINT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 3;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &shadow_volume_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow volume view\n");
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
        sampler_info.maxLod = 2.0f;

        if (vkCreateSampler(device_, &sampler_info, nullptr, &shadow_volume_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow volume sampler\n");
            return false;
        }

        printf("  Shadow volume created: %ux%ux%u (3 mip levels)\n", width, height, depth);
        return true;
    }

    void Renderer::destroy_shadow_volume_resources()
    {
        if (shadow_volume_sampler_)
        {
            vkDestroySampler(device_, shadow_volume_sampler_, nullptr);
            shadow_volume_sampler_ = VK_NULL_HANDLE;
        }
        if (shadow_volume_view_)
        {
            vkDestroyImageView(device_, shadow_volume_view_, nullptr);
            shadow_volume_view_ = VK_NULL_HANDLE;
        }
        if (shadow_volume_image_)
        {
            vkDestroyImage(device_, shadow_volume_image_, nullptr);
            shadow_volume_image_ = VK_NULL_HANDLE;
        }
        if (shadow_volume_memory_)
        {
            vkFreeMemory(device_, shadow_volume_memory_, nullptr);
            shadow_volume_memory_ = VK_NULL_HANDLE;
        }
    }

    void Renderer::upload_shadow_volume(const uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0,
                                        const uint8_t *mip1, uint32_t w1, uint32_t h1, uint32_t d1,
                                        const uint8_t *mip2, uint32_t w2, uint32_t h2, uint32_t d2)
    {
        if (!mip0 || !shadow_volume_image_)
            return;

        size_t size0 = static_cast<size_t>(w0) * h0 * d0;
        size_t size1 = static_cast<size_t>(w1) * h1 * d1;
        size_t size2 = static_cast<size_t>(w2) * h2 * d2;
        VkDeviceSize total_size = static_cast<VkDeviceSize>(size0 + size1 + size2);

        VulkanBuffer staging{};
        create_buffer(total_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging);

        void *mapped = nullptr;
        vkMapMemory(device_, staging.memory, 0, total_size, 0, &mapped);
        uint8_t *dst = static_cast<uint8_t *>(mapped);
        memcpy(dst, mip0, size0);
        dst += size0;
        if (mip1)
            memcpy(dst, mip1, size1);
        dst += size1;
        if (mip2)
            memcpy(dst, mip2, size2);
        vkUnmapMemory(device_, staging.memory);

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = command_pool_;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = shadow_volume_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 3;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy regions[3]{};
        regions[0].bufferOffset = 0;
        regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[0].imageSubresource.mipLevel = 0;
        regions[0].imageSubresource.baseArrayLayer = 0;
        regions[0].imageSubresource.layerCount = 1;
        regions[0].imageExtent = {w0, h0, d0};

        regions[1].bufferOffset = static_cast<VkDeviceSize>(size0);
        regions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[1].imageSubresource.mipLevel = 1;
        regions[1].imageSubresource.baseArrayLayer = 0;
        regions[1].imageSubresource.layerCount = 1;
        regions[1].imageExtent = {w1, h1, d1};

        regions[2].bufferOffset = static_cast<VkDeviceSize>(size0 + size1);
        regions[2].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[2].imageSubresource.mipLevel = 2;
        regions[2].imageSubresource.baseArrayLayer = 0;
        regions[2].imageSubresource.layerCount = 1;
        regions[2].imageExtent = {w2, h2, d2};

        vkCmdCopyBufferToImage(cmd, staging.buffer, shadow_volume_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 3, regions);

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
        destroy_buffer(&staging);
    }

    bool Renderer::create_blue_noise_texture()
    {
        constexpr uint32_t NOISE_SIZE = 128;
        std::vector<uint8_t> noise(NOISE_SIZE * NOISE_SIZE);

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

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = NOISE_SIZE;
        image_info.extent.height = NOISE_SIZE;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8_UNORM;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &blue_noise_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create blue noise image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, blue_noise_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &blue_noise_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate blue noise memory\n");
            return false;
        }

        vkBindImageMemory(device_, blue_noise_image_, blue_noise_memory_, 0);

        VulkanBuffer staging{};
        VkDeviceSize data_size = NOISE_SIZE * NOISE_SIZE;
        create_buffer(data_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging);

        void *mapped = nullptr;
        vkMapMemory(device_, staging.memory, 0, data_size, 0, &mapped);
        memcpy(mapped, noise.data(), data_size);
        vkUnmapMemory(device_, staging.memory);

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
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = blue_noise_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {NOISE_SIZE, NOISE_SIZE, 1};

        vkCmdCopyBufferToImage(cmd, staging.buffer, blue_noise_image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue_);

        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        destroy_buffer(&staging);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = blue_noise_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &blue_noise_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create blue noise view\n");
            return false;
        }

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        if (vkCreateSampler(device_, &sampler_info, nullptr, &blue_noise_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create blue noise sampler\n");
            return false;
        }

        printf("  Blue noise texture created: %ux%u\n", NOISE_SIZE, NOISE_SIZE);
        return true;
    }

    void Renderer::destroy_blue_noise_texture()
    {
        if (blue_noise_sampler_)
        {
            vkDestroySampler(device_, blue_noise_sampler_, nullptr);
            blue_noise_sampler_ = VK_NULL_HANDLE;
        }
        if (blue_noise_view_)
        {
            vkDestroyImageView(device_, blue_noise_view_, nullptr);
            blue_noise_view_ = VK_NULL_HANDLE;
        }
        if (blue_noise_image_)
        {
            vkDestroyImage(device_, blue_noise_image_, nullptr);
            blue_noise_image_ = VK_NULL_HANDLE;
        }
        if (blue_noise_memory_)
        {
            vkFreeMemory(device_, blue_noise_memory_, nullptr);
            blue_noise_memory_ = VK_NULL_HANDLE;
        }
    }

    bool Renderer::create_motion_vector_resources()
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = swapchain_extent_.width;
        image_info.extent.height = swapchain_extent_.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R16G16_SFLOAT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &motion_vector_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create motion vector image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, motion_vector_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &motion_vector_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate motion vector memory\n");
            return false;
        }

        vkBindImageMemory(device_, motion_vector_image_, motion_vector_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = motion_vector_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R16G16_SFLOAT;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &motion_vector_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create motion vector view\n");
            return false;
        }

        printf("  Motion vector buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    void Renderer::destroy_motion_vector_resources()
    {
        if (motion_vector_view_)
        {
            vkDestroyImageView(device_, motion_vector_view_, nullptr);
            motion_vector_view_ = VK_NULL_HANDLE;
        }
        if (motion_vector_image_)
        {
            vkDestroyImage(device_, motion_vector_image_, nullptr);
            motion_vector_image_ = VK_NULL_HANDLE;
        }
        if (motion_vector_memory_)
        {
            vkFreeMemory(device_, motion_vector_memory_, nullptr);
            motion_vector_memory_ = VK_NULL_HANDLE;
        }
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
        /* Use load render pass if compute already filled the gbuffer */
        rp_info.renderPass = gbuffer_compute_dispatched_ ? gbuffer_render_pass_load_ : gbuffer_render_pass_;
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
        gbuffer_compute_dispatched_ = false;
    }

    void Renderer::prepare_gbuffer_compute(const VoxelVolume *vol, const VoxelObjectWorld *objects)
    {
        if (!gbuffer_initialized_ || !vol || !voxel_resources_initialized_)
            return;

        /* Dispatch compute terrain + objects if enabled (must be called before begin_gbuffer_pass) */
        if (compute_raymarching_enabled_ && compute_resources_initialized_ && gbuffer_compute_pipeline_)
        {
            int32_t object_count = (objects && vobj_resources_initialized_) ? objects->object_count : 0;
            dispatch_gbuffer_compute(vol, object_count);
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
