#include "renderer.h"
#include "shaders_embedded.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    static constexpr uint32_t MAX_PARTICLE_INSTANCES = 65536;

    struct ParticleGPU
    {
        float position[3];
        float radius;
        float color[3];
        float flags; // 1.0 = active
    };

    static_assert(sizeof(ParticleGPU) == 32, "ParticleGPU must be 32 bytes");

    bool Renderer::init_particle_resources()
    {
        if (particle_resources_initialized_)
            return true;

        // Create particle SSBO
        VkDeviceSize buffer_size = MAX_PARTICLE_INSTANCES * sizeof(ParticleGPU);

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &buffer_info, nullptr, &particle_ssbo_.buffer) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create particle SSBO\n");
            return false;
        }

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(device_, particle_ssbo_.buffer, &mem_req);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_req.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &particle_ssbo_.memory) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate particle SSBO memory\n");
            return false;
        }

        vkBindBufferMemory(device_, particle_ssbo_.buffer, particle_ssbo_.memory, 0);
        particle_ssbo_.size = buffer_size;

        // Create descriptor set layout
        VkDescriptorSetLayoutBinding ssbo_binding{};
        ssbo_binding.binding = 0;
        ssbo_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssbo_binding.descriptorCount = 1;
        ssbo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &ssbo_binding;

        if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &particle_descriptor_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create particle descriptor layout\n");
            return false;
        }

        // Create descriptor pool
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &particle_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create particle descriptor pool\n");
            return false;
        }

        // Allocate descriptor set
        VkDescriptorSetAllocateInfo desc_alloc_info{};
        desc_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        desc_alloc_info.descriptorPool = particle_descriptor_pool_;
        desc_alloc_info.descriptorSetCount = 1;
        desc_alloc_info.pSetLayouts = &particle_descriptor_layout_;

        if (vkAllocateDescriptorSets(device_, &desc_alloc_info, &particle_descriptor_set_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate particle descriptor set\n");
            return false;
        }

        // Update descriptor set
        VkDescriptorBufferInfo desc_buffer_info{};
        desc_buffer_info.buffer = particle_ssbo_.buffer;
        desc_buffer_info.offset = 0;
        desc_buffer_info.range = buffer_size;

        VkWriteDescriptorSet desc_write{};
        desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        desc_write.dstSet = particle_descriptor_set_;
        desc_write.dstBinding = 0;
        desc_write.dstArrayElement = 0;
        desc_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        desc_write.descriptorCount = 1;
        desc_write.pBufferInfo = &desc_buffer_info;

        vkUpdateDescriptorSets(device_, 1, &desc_write, 0, nullptr);

        // Create pipeline
        if (!create_particle_pipeline())
        {
            fprintf(stderr, "Failed to create particle pipeline\n");
            return false;
        }

        particle_resources_initialized_ = true;
        printf("  Particle raymarching resources initialized\n");
        return true;
    }

    bool Renderer::create_particle_pipeline()
    {
        // Push constants
        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = 96; // mat4 view_proj (64) + vec3 camera_pos (12) + float pad (4) + int count (4) + int pad[3] (12)

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &particle_descriptor_layout_;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &particle_pipeline_layout_) != VK_SUCCESS)
        {
            return false;
        }

        // Shader modules
        VkShaderModuleCreateInfo vert_info{};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = shaders::k_shader_particle_vert_spv_size;
        vert_info.pCode = shaders::k_shader_particle_vert_spv;

        VkShaderModule vert_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &vert_info, nullptr, &vert_module) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create particle vertex shader module\n");
            return false;
        }

        VkShaderModuleCreateInfo frag_info{};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = shaders::k_shader_particle_frag_spv_size;
        frag_info.pCode = shaders::k_shader_particle_frag_spv;

        VkShaderModule frag_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &frag_info, nullptr, &frag_module) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create particle fragment shader module\n");
            vkDestroyShaderModule(device_, vert_module, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo vert_stage{};
        vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vert_stage.module = vert_module;
        vert_stage.pName = "main";

        VkPipelineShaderStageCreateInfo frag_stage{};
        frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage.module = frag_module;
        frag_stage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

        // Vertex input (none - generated in shader)
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
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Render all faces for proxy cube
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

        // G-buffer has 5 color attachments
        VkPipelineColorBlendAttachmentState blend_attachments[5] = {};
        for (int i = 0; i < 5; i++)
        {
            blend_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend_attachments[i].blendEnable = VK_FALSE;
        }

        VkPipelineColorBlendStateCreateInfo color_blend{};
        color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.attachmentCount = 5;
        color_blend.pAttachments = blend_attachments;

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
        pipeline_info.layout = particle_pipeline_layout_;
        pipeline_info.renderPass = gbuffer_render_pass_;
        pipeline_info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &particle_pipeline_);

        vkDestroyShaderModule(device_, vert_module, nullptr);
        vkDestroyShaderModule(device_, frag_module, nullptr);

        return result == VK_SUCCESS;
    }

    void Renderer::destroy_particle_resources()
    {
        if (!particle_resources_initialized_)
            return;

        vkDeviceWaitIdle(device_);

        if (particle_pipeline_)
        {
            vkDestroyPipeline(device_, particle_pipeline_, nullptr);
            particle_pipeline_ = VK_NULL_HANDLE;
        }

        if (particle_pipeline_layout_)
        {
            vkDestroyPipelineLayout(device_, particle_pipeline_layout_, nullptr);
            particle_pipeline_layout_ = VK_NULL_HANDLE;
        }

        if (particle_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, particle_descriptor_pool_, nullptr);
            particle_descriptor_pool_ = VK_NULL_HANDLE;
        }

        if (particle_descriptor_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, particle_descriptor_layout_, nullptr);
            particle_descriptor_layout_ = VK_NULL_HANDLE;
        }

        if (particle_ssbo_.buffer)
        {
            vkDestroyBuffer(device_, particle_ssbo_.buffer, nullptr);
            particle_ssbo_.buffer = VK_NULL_HANDLE;
        }

        if (particle_ssbo_.memory)
        {
            vkFreeMemory(device_, particle_ssbo_.memory, nullptr);
            particle_ssbo_.memory = VK_NULL_HANDLE;
        }

        particle_resources_initialized_ = false;
    }

    void Renderer::render_particles_raymarched(const ParticleSystem *sys)
    {
        if (!sys || sys->count == 0)
            return;

        if (!particle_resources_initialized_)
        {
            if (!init_particle_resources())
                return;
        }

        // Count active particles and upload to SSBO
        uint32_t active_count = 0;
        ParticleGPU *gpu_data = nullptr;

        vkMapMemory(device_, particle_ssbo_.memory, 0, MAX_PARTICLE_INSTANCES * sizeof(ParticleGPU), 0,
                    reinterpret_cast<void **>(&gpu_data));

        float alpha = interp_alpha_;

        for (int32_t i = 0; i < sys->count && active_count < MAX_PARTICLE_INSTANCES; i++)
        {
            const Particle *p = &sys->particles[i];
            if (!p->active)
                continue;

            /* Interpolate between previous and current position for smooth rendering */
            float interp_x = p->prev_position.x + alpha * (p->position.x - p->prev_position.x);
            float interp_y = p->prev_position.y + alpha * (p->position.y - p->prev_position.y);
            float interp_z = p->prev_position.z + alpha * (p->position.z - p->prev_position.z);

            gpu_data[active_count].position[0] = interp_x;
            gpu_data[active_count].position[1] = interp_y;
            gpu_data[active_count].position[2] = interp_z;
            gpu_data[active_count].radius = p->radius;
            gpu_data[active_count].color[0] = p->color.x;
            gpu_data[active_count].color[1] = p->color.y;
            gpu_data[active_count].color[2] = p->color.z;
            gpu_data[active_count].flags = 1.0f;

            active_count++;
        }

        vkUnmapMemory(device_, particle_ssbo_.memory);

        if (active_count == 0)
            return;

        // Bind pipeline and descriptor set
        vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, particle_pipeline_);
        vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                particle_pipeline_layout_, 0, 1, &particle_descriptor_set_, 0, nullptr);

        // Push constants
        struct
        {
            float view_proj[16];
            float camera_pos[3];
            float pad1;
            int32_t particle_count;
            float near_plane;
            float far_plane;
            int32_t pad2;
        } pc;

        Mat4 vp = mat4_multiply(projection_matrix_, view_matrix_);
        memcpy(pc.view_proj, &vp.m[0], 64);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.pad1 = 0.0f;
        pc.particle_count = static_cast<int32_t>(active_count);
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.pad2 = 0;

        vkCmdPushConstants(command_buffers_[current_frame_], particle_pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        // Draw instanced (36 vertices per cube, active_count instances)
        vkCmdDraw(command_buffers_[current_frame_], 36, active_count, 0, 0);
    }

} // namespace patch
