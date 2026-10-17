#include "vk_pipeline.h"
#include "vk_context.h"

namespace patch
{

    bool create_set_layout(VkDevice device, std::initializer_list<BindingDesc> bindings, VkDescriptorSetLayout *out)
    {
        VkDescriptorSetLayoutBinding layout_bindings[16]{};
        uint32_t count = 0;
        for (const BindingDesc &b : bindings)
        {
            if (count == 16)
                return false;
            layout_bindings[count].binding = count;
            layout_bindings[count].descriptorType = b.type;
            layout_bindings[count].descriptorCount = 1;
            layout_bindings[count].stageFlags = b.stages;
            count++;
        }

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = count;
        layout_info.pBindings = layout_bindings;
        return vkCreateDescriptorSetLayout(device, &layout_info, nullptr, out) == VK_SUCCESS;
    }

    bool create_pipeline_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> set_layouts,
                                VkShaderStageFlags push_stages, uint32_t push_size, VkPipelineLayout *out)
    {
        VkPushConstantRange push_range{};
        push_range.stageFlags = push_stages;
        push_range.offset = 0;
        push_range.size = push_size;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
        layout_info.pSetLayouts = set_layouts.begin();
        layout_info.pushConstantRangeCount = push_size > 0 ? 1 : 0;
        layout_info.pPushConstantRanges = push_size > 0 ? &push_range : nullptr;
        return vkCreatePipelineLayout(device, &layout_info, nullptr, out) == VK_SUCCESS;
    }

    bool create_descriptor_pool(VkDevice device, uint32_t max_sets, std::initializer_list<VkDescriptorPoolSize> sizes,
                                VkDescriptorPool *out)
    {
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pool_info.pPoolSizes = sizes.begin();
        pool_info.maxSets = max_sets;
        return vkCreateDescriptorPool(device, &pool_info, nullptr, out) == VK_SUCCESS;
    }

    bool allocate_sets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout, uint32_t count,
                       VkDescriptorSet *out)
    {
        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        if (count > MAX_FRAMES_IN_FLIGHT)
            return false;
        for (uint32_t i = 0; i < count; i++)
            layouts[i] = layout;

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = pool;
        alloc_info.descriptorSetCount = count;
        alloc_info.pSetLayouts = layouts;
        return vkAllocateDescriptorSets(device, &alloc_info, out) == VK_SUCCESS;
    }

    static bool create_shader_module(VkDevice device, ShaderCode shader, VkShaderModule *out)
    {
        VkShaderModuleCreateInfo module_info{};
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = shader.size;
        module_info.pCode = shader.code;
        return vkCreateShaderModule(device, &module_info, nullptr, out) == VK_SUCCESS;
    }

    bool create_compute_pipeline(const VulkanContext &ctx, ShaderCode shader, VkPipelineLayout layout,
                                 VkPipeline *out)
    {
        VkShaderModule module = VK_NULL_HANDLE;
        if (!create_shader_module(ctx.device(), shader, &module))
            return false;

        VkComputePipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module = module;
        pipeline_info.stage.pName = "main";
        pipeline_info.layout = layout;

        VkResult result = vkCreateComputePipelines(ctx.device(), ctx.pipeline_cache(), 1, &pipeline_info, nullptr, out);
        vkDestroyShaderModule(ctx.device(), module, nullptr);
        return result == VK_SUCCESS;
    }

    bool create_graphics_pipeline(const VulkanContext &ctx, const GraphicsPipelineDesc &desc, VkPipeline *out)
    {
        constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;
        if (desc.color_count > MAX_COLOR_ATTACHMENTS)
            return false;

        VkDevice device = ctx.device();
        VkShaderModule vert_module = VK_NULL_HANDLE;
        VkShaderModule frag_module = VK_NULL_HANDLE;
        if (!create_shader_module(device, desc.vertex, &vert_module))
            return false;
        if (!create_shader_module(device, desc.fragment, &frag_module))
        {
            vkDestroyShaderModule(device, vert_module, nullptr);
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

        /* Fullscreen and proxy-geometry passes generate their vertices in the vertex shader */
        VkPipelineVertexInputStateCreateInfo no_vertex_input{};
        no_vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = desc.cull_mode;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
        depth_stencil.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
        depth_stencil.depthCompareOp = desc.depth_compare;

        VkPipelineColorBlendAttachmentState blend_attachments[MAX_COLOR_ATTACHMENTS]{};
        for (uint32_t i = 0; i < desc.color_count; i++)
        {
            VkPipelineColorBlendAttachmentState &b = blend_attachments[i];
            b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            if (desc.alpha_blend)
            {
                b.blendEnable = VK_TRUE;
                b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                b.colorBlendOp = VK_BLEND_OP_ADD;
                b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                b.alphaBlendOp = VK_BLEND_OP_ADD;
            }
        }

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.attachmentCount = desc.color_count;
        color_blending.pAttachments = blend_attachments;

        VkPipelineRenderingCreateInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering_info.colorAttachmentCount = desc.color_count;
        rendering_info.pColorAttachmentFormats = desc.color_formats;
        rendering_info.depthAttachmentFormat = desc.depth_format;

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.pNext = &rendering_info;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = desc.vertex_input ? desc.vertex_input : &no_vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = desc.layout;

        VkResult result = vkCreateGraphicsPipelines(device, ctx.pipeline_cache(), 1, &pipeline_info, nullptr, out);

        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return result == VK_SUCCESS;
    }

    void destroy_pipeline(VkDevice device, VkPipeline *pipeline)
    {
        if (*pipeline)
            vkDestroyPipeline(device, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }

    void destroy_pipeline_layout(VkDevice device, VkPipelineLayout *layout)
    {
        if (*layout)
            vkDestroyPipelineLayout(device, *layout, nullptr);
        *layout = VK_NULL_HANDLE;
    }

    void destroy_set_layout(VkDevice device, VkDescriptorSetLayout *layout)
    {
        if (*layout)
            vkDestroyDescriptorSetLayout(device, *layout, nullptr);
        *layout = VK_NULL_HANDLE;
    }

    void destroy_descriptor_pool(VkDevice device, VkDescriptorPool *pool)
    {
        if (*pool)
            vkDestroyDescriptorPool(device, *pool, nullptr);
        *pool = VK_NULL_HANDLE;
    }

    void destroy_sampler(VkDevice device, VkSampler *sampler)
    {
        if (*sampler)
            vkDestroySampler(device, *sampler, nullptr);
        *sampler = VK_NULL_HANDLE;
    }

    bool create_point_sampler(VkDevice device, VkSamplerAddressMode address_mode, float max_lod, VkSampler *out)
    {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = address_mode;
        sampler_info.addressModeV = address_mode;
        sampler_info.addressModeW = address_mode;
        sampler_info.maxLod = max_lod;
        return vkCreateSampler(device, &sampler_info, nullptr, out) == VK_SUCCESS;
    }

    bool create_linear_sampler(VkDevice device, VkSamplerAddressMode address_mode, VkSampler *out)
    {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = address_mode;
        sampler_info.addressModeV = address_mode;
        sampler_info.addressModeW = address_mode;
        return vkCreateSampler(device, &sampler_info, nullptr, out) == VK_SUCCESS;
    }

    VkWriteDescriptorSet &DescriptorWriter::next(VkDescriptorSet set, uint32_t binding, VkDescriptorType type)
    {
        if (count_ == MAX_WRITES)
            update();
        VkWriteDescriptorSet &w = writes_[count_];
        w = {};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorType = type;
        w.descriptorCount = 1;
        return w;
    }

    DescriptorWriter &DescriptorWriter::sampled(VkDescriptorSet set, uint32_t binding, VkSampler sampler, VkImageView view)
    {
        VkWriteDescriptorSet &w = next(set, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        images_[count_] = {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        w.pImageInfo = &images_[count_++];
        return *this;
    }

    DescriptorWriter &DescriptorWriter::storage(VkDescriptorSet set, uint32_t binding, VkImageView view)
    {
        VkWriteDescriptorSet &w = next(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        images_[count_] = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
        w.pImageInfo = &images_[count_++];
        return *this;
    }

    DescriptorWriter &DescriptorWriter::buffer(VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                                               VkBuffer buffer, VkDeviceSize range)
    {
        VkWriteDescriptorSet &w = next(set, binding, type);
        buffers_[count_] = {buffer, 0, range};
        w.pBufferInfo = &buffers_[count_++];
        return *this;
    }

    void DescriptorWriter::update()
    {
        if (count_ > 0)
            vkUpdateDescriptorSets(device_, count_, writes_, 0, nullptr);
        count_ = 0;
    }

}
