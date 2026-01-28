#include "renderer.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    bool Renderer::init_compute_raymarching()
    {
        printf("Initializing compute raymarching pipelines...\n");

        if (!create_shadow_output_resources())
        {
            fprintf(stderr, "Failed to create shadow output resources\n");
            return false;
        }

        if (!create_shadow_history_resources())
        {
            fprintf(stderr, "Failed to create shadow history resources\n");
            return false;
        }

        if (!create_gbuffer_compute_pipeline())
        {
            fprintf(stderr, "Failed to create G-buffer compute pipeline\n");
            return false;
        }

        if (!create_shadow_compute_pipeline())
        {
            fprintf(stderr, "Failed to create shadow compute pipeline\n");
            return false;
        }

        if (!create_temporal_shadow_pipeline())
        {
            fprintf(stderr, "Failed to create temporal shadow pipeline\n");
            return false;
        }

        if (!create_temporal_shadow_descriptor_sets())
        {
            fprintf(stderr, "Failed to create temporal shadow descriptor sets\n");
            return false;
        }

        history_write_index_ = 0;
        temporal_shadow_history_valid_ = false;

        /* Initialize AO compute resources */
        if (!create_ao_output_resources())
        {
            fprintf(stderr, "Failed to create AO output resources\n");
            return false;
        }

        if (!create_ao_history_resources())
        {
            fprintf(stderr, "Failed to create AO history resources\n");
            return false;
        }

        if (!create_ao_compute_pipeline())
        {
            fprintf(stderr, "Failed to create AO compute pipeline\n");
            return false;
        }

        if (!create_temporal_ao_pipeline())
        {
            fprintf(stderr, "Failed to create temporal AO pipeline\n");
            return false;
        }

        ao_history_write_index_ = 0;
        temporal_ao_history_valid_ = false;
        ao_resources_initialized_ = true;

        /* Initialize spatial denoise resources */
        if (!create_lit_color_resources())
        {
            fprintf(stderr, "Failed to create lit color resources\n");
            return false;
        }

        if (!create_denoised_color_resources())
        {
            fprintf(stderr, "Failed to create denoised color resources\n");
            return false;
        }

        if (!create_spatial_denoise_pipeline())
        {
            fprintf(stderr, "Failed to create spatial denoise pipeline\n");
            return false;
        }

        if (!create_spatial_denoise_descriptor_sets())
        {
            fprintf(stderr, "Failed to create spatial denoise descriptor sets\n");
            return false;
        }

        if (!create_deferred_lighting_intermediate_fb())
        {
            fprintf(stderr, "Failed to create deferred lighting intermediate framebuffer\n");
            return false;
        }

        spatial_denoise_initialized_ = true;

        /* Initialize TAA resources */
        if (!create_taa_history_resources())
        {
            fprintf(stderr, "Failed to create TAA history resources\n");
            return false;
        }

        if (!create_taa_pipeline())
        {
            fprintf(stderr, "Failed to create TAA pipeline\n");
            return false;
        }

        if (!create_taa_descriptor_sets())
        {
            fprintf(stderr, "Failed to create TAA descriptor sets\n");
            return false;
        }

        taa_history_write_index_ = 0;
        taa_history_valid_ = false;

        compute_resources_initialized_ = true;
        printf("  Compute raymarching pipelines initialized\n");
        return true;
    }

    bool Renderer::create_gbuffer_compute_pipeline()
    {
        /* Set 0: Terrain data (voxel buffer, chunk headers, material palette, temporal UBO) */
        VkDescriptorSetLayoutBinding terrain_bindings[4]{};
        terrain_bindings[0].binding = 0;
        terrain_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        terrain_bindings[0].descriptorCount = 1;
        terrain_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        terrain_bindings[1].binding = 1;
        terrain_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        terrain_bindings[1].descriptorCount = 1;
        terrain_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        terrain_bindings[2].binding = 2;
        terrain_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        terrain_bindings[2].descriptorCount = 1;
        terrain_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        terrain_bindings[3].binding = 3;
        terrain_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        terrain_bindings[3].descriptorCount = 1;
        terrain_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo terrain_layout_info{};
        terrain_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        terrain_layout_info.bindingCount = 4;
        terrain_layout_info.pBindings = terrain_bindings;

        if (vkCreateDescriptorSetLayout(device_, &terrain_layout_info, nullptr, &gbuffer_compute_terrain_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create gbuffer compute terrain layout\n");
            return false;
        }

        /* Set 1: Voxel objects (atlas sampler, metadata buffer, spatial grid) */
        VkDescriptorSetLayoutBinding vobj_bindings[3]{};
        vobj_bindings[0].binding = 0;
        vobj_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        vobj_bindings[0].descriptorCount = 1;
        vobj_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        vobj_bindings[1].binding = 1;
        vobj_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vobj_bindings[1].descriptorCount = 1;
        vobj_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        vobj_bindings[2].binding = 2;
        vobj_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vobj_bindings[2].descriptorCount = 1;
        vobj_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo vobj_layout_info{};
        vobj_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        vobj_layout_info.bindingCount = 3;
        vobj_layout_info.pBindings = vobj_bindings;

        if (vkCreateDescriptorSetLayout(device_, &vobj_layout_info, nullptr, &gbuffer_compute_vobj_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create gbuffer compute vobj layout\n");
            return false;
        }

        /* Set 2: G-buffer output images (albedo, normal, material, depth, world_pos, motion_vector) */
        VkDescriptorSetLayoutBinding output_bindings[6]{};
        for (int i = 0; i < 6; i++)
        {
            output_bindings[i].binding = i;
            output_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_bindings[i].descriptorCount = 1;
            output_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 6;
        output_layout_info.pBindings = output_bindings;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &gbuffer_compute_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create gbuffer compute output layout\n");
            return false;
        }

        /* Pipeline layout with 3 descriptor sets + push constants */
        VkDescriptorSetLayout set_layouts[3] = {
            gbuffer_compute_terrain_layout_,
            gbuffer_compute_vobj_layout_,
            gbuffer_compute_output_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = 256;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 3;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gbuffer_compute_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create gbuffer compute pipeline layout\n");
            return false;
        }

        /* Create compute pipeline */
        if (!create_compute_pipeline(
                shaders::k_shader_raymarch_gbuffer_comp_spv,
                shaders::k_shader_raymarch_gbuffer_comp_spv_size,
                gbuffer_compute_layout_,
                &gbuffer_compute_pipeline_))
        {
            fprintf(stderr, "Failed to create gbuffer compute pipeline\n");
            return false;
        }

        printf("  G-buffer compute pipeline created\n");
        return true;
    }

    bool Renderer::create_shadow_compute_pipeline()
    {
        /* Set 0: Terrain data for HDDA (voxel buffer, chunk headers, shadow volume, material palette) */
        VkDescriptorSetLayoutBinding input_bindings[4]{};
        input_bindings[0].binding = 0;
        input_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        input_bindings[0].descriptorCount = 1;
        input_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        input_bindings[1].binding = 1;
        input_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        input_bindings[1].descriptorCount = 1;
        input_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        input_bindings[2].binding = 2;
        input_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        input_bindings[2].descriptorCount = 1;
        input_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        input_bindings[3].binding = 3;
        input_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        input_bindings[3].descriptorCount = 1;
        input_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo input_layout_info{};
        input_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        input_layout_info.bindingCount = 4;
        input_layout_info.pBindings = input_bindings;

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &shadow_compute_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow compute input layout\n");
            return false;
        }

        /* Set 1: G-buffer samplers (depth, normal, world_pos, blue noise) */
        VkDescriptorSetLayoutBinding gbuffer_bindings[4]{};
        gbuffer_bindings[0].binding = 0;
        gbuffer_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbuffer_bindings[0].descriptorCount = 1;
        gbuffer_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        gbuffer_bindings[1].binding = 1;
        gbuffer_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbuffer_bindings[1].descriptorCount = 1;
        gbuffer_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        gbuffer_bindings[2].binding = 2;
        gbuffer_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbuffer_bindings[2].descriptorCount = 1;
        gbuffer_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        gbuffer_bindings[3].binding = 3;
        gbuffer_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbuffer_bindings[3].descriptorCount = 1;
        gbuffer_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo gbuffer_layout_info{};
        gbuffer_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        gbuffer_layout_info.bindingCount = 4;
        gbuffer_layout_info.pBindings = gbuffer_bindings;

        if (vkCreateDescriptorSetLayout(device_, &gbuffer_layout_info, nullptr, &shadow_compute_gbuffer_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow compute gbuffer layout\n");
            return false;
        }

        /* Set 2: Shadow output image */
        VkDescriptorSetLayoutBinding output_binding{};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 1;
        output_layout_info.pBindings = &output_binding;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &shadow_compute_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow compute output layout\n");
            return false;
        }

        /* Pipeline layout with 4 descriptor sets
         * Set 3 reuses gbuffer_compute_vobj_layout_ since bindings are identical */
        VkDescriptorSetLayout set_layouts[4] = {
            shadow_compute_input_layout_,
            shadow_compute_gbuffer_layout_,
            shadow_compute_output_layout_,
            gbuffer_compute_vobj_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = 256;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 4;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &shadow_compute_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow compute pipeline layout\n");
            return false;
        }

        /* Create compute pipeline */
        if (!create_compute_pipeline(
                shaders::k_shader_raymarch_shadow_comp_spv,
                shaders::k_shader_raymarch_shadow_comp_spv_size,
                shadow_compute_layout_,
                &shadow_compute_pipeline_))
        {
            fprintf(stderr, "Failed to create shadow compute pipeline\n");
            return false;
        }

        printf("  Shadow compute pipeline created\n");
        return true;
    }

    bool Renderer::create_gbuffer_compute_descriptor_sets()
    {
        if (!compute_resources_initialized_ || voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return true;

        /* Create descriptor pool */
        VkDescriptorPoolSize pool_sizes[4]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 4; /* voxel data, headers, vobj metadata, spatial grid */
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; /* material palette, temporal ubo */
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 1; /* vobj atlas */
        pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[3].descriptorCount = MAX_FRAMES_IN_FLIGHT * 6; /* 6 G-buffer outputs (incl world_pos, motion vector) */

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 4;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 3;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &gbuffer_compute_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create gbuffer compute descriptor pool\n");
            return false;
        }

        /* Allocate descriptor sets */
        VkDescriptorSetLayout terrain_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout vobj_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout output_layouts[MAX_FRAMES_IN_FLIGHT];

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            terrain_layouts[i] = gbuffer_compute_terrain_layout_;
            vobj_layouts[i] = gbuffer_compute_vobj_layout_;
            output_layouts[i] = gbuffer_compute_output_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = gbuffer_compute_descriptor_pool_;

        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        alloc_info.pSetLayouts = terrain_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, gbuffer_compute_terrain_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate gbuffer compute terrain sets\n");
            return false;
        }

        alloc_info.pSetLayouts = vobj_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, gbuffer_compute_vobj_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate gbuffer compute vobj sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, gbuffer_compute_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate gbuffer compute output sets\n");
            return false;
        }

        /* Update descriptor sets */
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            /* Set 0: Terrain data */
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

            VkWriteDescriptorSet terrain_writes[4]{};
            terrain_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            terrain_writes[0].dstSet = gbuffer_compute_terrain_sets_[i];
            terrain_writes[0].dstBinding = 0;
            terrain_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            terrain_writes[0].descriptorCount = 1;
            terrain_writes[0].pBufferInfo = &voxel_data_info;

            terrain_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            terrain_writes[1].dstSet = gbuffer_compute_terrain_sets_[i];
            terrain_writes[1].dstBinding = 1;
            terrain_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            terrain_writes[1].descriptorCount = 1;
            terrain_writes[1].pBufferInfo = &headers_info;

            terrain_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            terrain_writes[2].dstSet = gbuffer_compute_terrain_sets_[i];
            terrain_writes[2].dstBinding = 2;
            terrain_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            terrain_writes[2].descriptorCount = 1;
            terrain_writes[2].pBufferInfo = &material_info;

            terrain_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            terrain_writes[3].dstSet = gbuffer_compute_terrain_sets_[i];
            terrain_writes[3].dstBinding = 3;
            terrain_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            terrain_writes[3].descriptorCount = 1;
            terrain_writes[3].pBufferInfo = &temporal_info;

            vkUpdateDescriptorSets(device_, 4, terrain_writes, 0, nullptr);

            /* Set 1: Voxel objects + BVH
             * Must always write valid descriptors (shadow shader binds this set unconditionally).
             * Use fallback resources when vobj isn't ready - shader uses object_count=0 to skip tracing. */
            VkDescriptorImageInfo atlas_info{};
            VkDescriptorBufferInfo vobj_meta_info{};
            VkDescriptorBufferInfo bvh_info{};

            if (vobj_atlas_view_ && vobj_atlas_sampler_)
            {
                atlas_info.sampler = vobj_atlas_sampler_;
                atlas_info.imageView = vobj_atlas_view_;
                atlas_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                vobj_meta_info.buffer = vobj_metadata_buffer_[i].buffer;
                vobj_meta_info.offset = 0;
                vobj_meta_info.range = VK_WHOLE_SIZE;

                bvh_info.buffer = bvh_buffer_.buffer;
                bvh_info.offset = 0;
                bvh_info.range = sizeof(GPUBVHBuffer);
            }
            else
            {
                /* Fallback: use shadow volume texture and voxel data buffer as dummy bindings */
                atlas_info.sampler = shadow_volume_sampler_ ? shadow_volume_sampler_ : gbuffer_sampler_;
                atlas_info.imageView = shadow_volume_view_ ? shadow_volume_view_ : gbuffer_views_[0];
                atlas_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                vobj_meta_info.buffer = voxel_data_buffer_.buffer;
                vobj_meta_info.offset = 0;
                vobj_meta_info.range = VK_WHOLE_SIZE;

                bvh_info.buffer = voxel_data_buffer_.buffer;
                bvh_info.offset = 0;
                bvh_info.range = VK_WHOLE_SIZE;
            }

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

            /* Set 2: G-buffer output images (albedo, normal, material, depth, world_pos, motion_vector) */
            VkDescriptorImageInfo gbuffer_infos[6]{};
            for (int g = 0; g < 5; g++)
            {
                gbuffer_infos[g].imageView = gbuffer_views_[g];
                gbuffer_infos[g].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            gbuffer_infos[5].imageView = motion_vector_view_;
            gbuffer_infos[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_writes[6]{};
            for (int g = 0; g < 6; g++)
            {
                output_writes[g].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                output_writes[g].dstSet = gbuffer_compute_output_sets_[i];
                output_writes[g].dstBinding = g;
                output_writes[g].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                output_writes[g].descriptorCount = 1;
                output_writes[g].pImageInfo = &gbuffer_infos[g];
            }

            vkUpdateDescriptorSets(device_, 6, output_writes, 0, nullptr);
        }

        printf("  G-buffer compute descriptor sets created\n");
        return true;
    }

    bool Renderer::create_shadow_compute_descriptor_sets()
    {
        if (!compute_resources_initialized_ || voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return true;

        /* Create descriptor pool - includes vobj data for direct object shadow tracing */
        VkDescriptorPoolSize pool_sizes[4]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 3; /* voxel_data, chunk_headers, vobj metadata */
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 6; /* depth, normal, world_pos, blue noise, shadow_volume, vobj_atlas */
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 1;
        pool_sizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[3].descriptorCount = MAX_FRAMES_IN_FLIGHT * 1; /* material palette */

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 4;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 4; /* 4 sets per frame: input, gbuffer, output, vobj */

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &shadow_compute_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow compute descriptor pool\n");
            return false;
        }

        /* Allocate descriptor sets */
        VkDescriptorSetLayout input_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout gbuffer_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout output_layouts[MAX_FRAMES_IN_FLIGHT];

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            input_layouts[i] = shadow_compute_input_layout_;
            gbuffer_layouts[i] = shadow_compute_gbuffer_layout_;
            output_layouts[i] = shadow_compute_output_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = shadow_compute_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, shadow_compute_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate shadow compute input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = gbuffer_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, shadow_compute_gbuffer_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate shadow compute gbuffer sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, shadow_compute_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate shadow compute output sets\n");
            return false;
        }

        /* Update descriptor sets */
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            /* Set 0: Terrain data for HDDA (voxel buffer, chunk headers, shadow volume, material palette) */
            VkDescriptorBufferInfo voxel_data_info{};
            voxel_data_info.buffer = voxel_data_buffer_.buffer;
            voxel_data_info.offset = 0;
            voxel_data_info.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo headers_info{};
            headers_info.buffer = voxel_headers_buffer_.buffer;
            headers_info.offset = 0;
            headers_info.range = VK_WHOLE_SIZE;

            VkDescriptorImageInfo shadow_vol_info{};
            shadow_vol_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadow_vol_info.imageView = shadow_volume_view_;
            shadow_vol_info.sampler = shadow_volume_sampler_;

            VkDescriptorBufferInfo material_info{};
            material_info.buffer = voxel_material_buffer_.buffer;
            material_info.offset = 0;
            material_info.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet input_writes[4]{};
            input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[0].dstSet = shadow_compute_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pBufferInfo = &voxel_data_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = shadow_compute_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pBufferInfo = &headers_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = shadow_compute_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &shadow_vol_info;

            input_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[3].dstSet = shadow_compute_input_sets_[i];
            input_writes[3].dstBinding = 3;
            input_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            input_writes[3].descriptorCount = 1;
            input_writes[3].pBufferInfo = &material_info;

            vkUpdateDescriptorSets(device_, shadow_volume_view_ ? 4 : 2, input_writes, 0, nullptr);

            /* Set 1: G-buffer samplers (depth, normal, world_pos, blue noise) */
            VkDescriptorImageInfo depth_info{};
            depth_info.sampler = gbuffer_sampler_;
            depth_info.imageView = gbuffer_views_[GBUFFER_LINEAR_DEPTH];
            depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normal_info{};
            normal_info.sampler = gbuffer_sampler_;
            normal_info.imageView = gbuffer_views_[GBUFFER_NORMAL];
            normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo world_pos_info{};
            world_pos_info.sampler = gbuffer_sampler_;
            world_pos_info.imageView = gbuffer_views_[GBUFFER_WORLD_POS];
            world_pos_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo noise_info{};
            noise_info.sampler = blue_noise_sampler_ ? blue_noise_sampler_ : gbuffer_sampler_;
            noise_info.imageView = blue_noise_view_ ? blue_noise_view_ : gbuffer_views_[0];
            noise_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet gbuffer_writes[4]{};
            gbuffer_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbuffer_writes[0].dstSet = shadow_compute_gbuffer_sets_[i];
            gbuffer_writes[0].dstBinding = 0;
            gbuffer_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[0].descriptorCount = 1;
            gbuffer_writes[0].pImageInfo = &depth_info;

            gbuffer_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbuffer_writes[1].dstSet = shadow_compute_gbuffer_sets_[i];
            gbuffer_writes[1].dstBinding = 1;
            gbuffer_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[1].descriptorCount = 1;
            gbuffer_writes[1].pImageInfo = &normal_info;

            gbuffer_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbuffer_writes[2].dstSet = shadow_compute_gbuffer_sets_[i];
            gbuffer_writes[2].dstBinding = 2;
            gbuffer_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[2].descriptorCount = 1;
            gbuffer_writes[2].pImageInfo = &world_pos_info;

            gbuffer_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbuffer_writes[3].dstSet = shadow_compute_gbuffer_sets_[i];
            gbuffer_writes[3].dstBinding = 3;
            gbuffer_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[3].descriptorCount = 1;
            gbuffer_writes[3].pImageInfo = &noise_info;

            vkUpdateDescriptorSets(device_, 4, gbuffer_writes, 0, nullptr);

            /* Set 2: Shadow output */
            VkDescriptorImageInfo output_info{};
            output_info.imageView = shadow_output_view_;
            output_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_write{};
            output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_write.dstSet = shadow_compute_output_sets_[i];
            output_write.dstBinding = 0;
            output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_write.descriptorCount = 1;
            output_write.pImageInfo = &output_info;

            vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
        }

        printf("  Shadow compute descriptor sets created\n");
        return true;
    }

    void Renderer::update_shadow_volume_descriptor()
    {
        if (!shadow_volume_view_ || !shadow_volume_sampler_ || !shadow_compute_descriptor_pool_)
            return;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo shadow_vol_info{};
            shadow_vol_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadow_vol_info.imageView = shadow_volume_view_;
            shadow_vol_info.sampler = shadow_volume_sampler_;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = shadow_compute_input_sets_[i];
            write.dstBinding = 2;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &shadow_vol_info;

            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

}
