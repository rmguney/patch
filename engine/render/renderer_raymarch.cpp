#include "renderer.h"
#include "voxel_push_constants.h"
#include "shaders_embedded.h"
#include <cstring>
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

        compute_resources_initialized_ = true;
        printf("  Compute raymarching pipelines initialized\n");
        return true;
    }

    bool Renderer::create_shadow_history_resources()
    {
        for (int i = 0; i < 2; i++)
        {
            if (history_images_[i] || history_image_views_[i] || history_image_memory_[i])
                continue;

            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.extent.width = swapchain_extent_.width;
            image_info.extent.height = swapchain_extent_.height;
            image_info.extent.depth = 1;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = VK_FORMAT_R8_UNORM;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(device_, &image_info, nullptr, &history_images_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create shadow history image %d\n", i);
                return false;
            }

            VkMemoryRequirements mem_reqs;
            vkGetImageMemoryRequirements(device_, history_images_[i], &mem_reqs);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_reqs.size;
            alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device_, &alloc_info, nullptr, &history_image_memory_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate shadow history memory %d\n", i);
                return false;
            }

            vkBindImageMemory(device_, history_images_[i], history_image_memory_[i], 0);

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = history_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = VK_FORMAT_R8_UNORM;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &history_image_views_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create shadow history view %d\n", i);
                return false;
            }
        }

        printf("  Shadow history buffers created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_temporal_shadow_pipeline()
    {
        /* Set 0: G-buffer samplers + current/history shadow */
        VkDescriptorSetLayoutBinding input_bindings[5]{};
        for (uint32_t b = 0; b < 5; b++)
        {
            input_bindings[b].binding = b;
            input_bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_bindings[b].descriptorCount = 1;
            input_bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo input_layout_info{};
        input_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        input_layout_info.bindingCount = 5;
        input_layout_info.pBindings = input_bindings;

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &temporal_shadow_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create temporal shadow input layout\n");
            return false;
        }

        /* Set 1: resolved shadow output */
        VkDescriptorSetLayoutBinding output_binding{};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 1;
        output_layout_info.pBindings = &output_binding;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &temporal_shadow_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create temporal shadow output layout\n");
            return false;
        }

        VkDescriptorSetLayout set_layouts[2] = {temporal_shadow_input_layout_, temporal_shadow_output_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = 256;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &temporal_compute_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create temporal shadow pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_temporal_shadow_comp_spv,
                shaders::k_shader_temporal_shadow_comp_spv_size,
                temporal_compute_layout_,
                &temporal_compute_pipeline_))
        {
            fprintf(stderr, "Failed to create temporal shadow compute pipeline\n");
            return false;
        }

        printf("  Temporal shadow pipeline created\n");
        return true;
    }

    bool Renderer::create_temporal_shadow_descriptor_sets()
    {
        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 5;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 2;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &temporal_shadow_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create temporal shadow descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout input_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout output_layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            input_layouts[i] = temporal_shadow_input_layout_;
            output_layouts[i] = temporal_shadow_output_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = temporal_shadow_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, temporal_shadow_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate temporal shadow input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, temporal_shadow_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate temporal shadow output sets\n");
            return false;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo depth_info{};
            depth_info.sampler = gbuffer_sampler_;
            depth_info.imageView = gbuffer_views_[GBUFFER_LINEAR_DEPTH];
            depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normal_info{};
            normal_info.sampler = gbuffer_sampler_;
            normal_info.imageView = gbuffer_views_[GBUFFER_NORMAL];
            normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo motion_info{};
            motion_info.sampler = gbuffer_sampler_;
            motion_info.imageView = motion_vector_view_ ? motion_vector_view_ : gbuffer_views_[0];
            motion_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo shadow_current_info{};
            shadow_current_info.sampler = gbuffer_sampler_;
            shadow_current_info.imageView = shadow_output_view_ ? shadow_output_view_ : gbuffer_views_[0];
            shadow_current_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo shadow_history_info{};
            shadow_history_info.sampler = gbuffer_sampler_;
            shadow_history_info.imageView = history_image_views_[0] ? history_image_views_[0] : gbuffer_views_[0];
            shadow_history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet input_writes[5]{};
            input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[0].dstSet = temporal_shadow_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pImageInfo = &depth_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = temporal_shadow_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pImageInfo = &normal_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = temporal_shadow_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &motion_info;

            input_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[3].dstSet = temporal_shadow_input_sets_[i];
            input_writes[3].dstBinding = 3;
            input_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[3].descriptorCount = 1;
            input_writes[3].pImageInfo = &shadow_current_info;

            input_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[4].dstSet = temporal_shadow_input_sets_[i];
            input_writes[4].dstBinding = 4;
            input_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[4].descriptorCount = 1;
            input_writes[4].pImageInfo = &shadow_history_info;

            vkUpdateDescriptorSets(device_, 5, input_writes, 0, nullptr);

            VkDescriptorImageInfo out_info{};
            out_info.imageView = history_image_views_[0] ? history_image_views_[0] : shadow_output_view_;
            out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_write{};
            output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_write.dstSet = temporal_shadow_output_sets_[i];
            output_write.dstBinding = 0;
            output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_write.descriptorCount = 1;
            output_write.pImageInfo = &out_info;

            vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
        }

        printf("  Temporal shadow descriptor sets created\n");
        return true;
    }

    bool Renderer::create_shadow_output_resources()
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = swapchain_extent_.width;
        image_info.extent.height = swapchain_extent_.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_R8_UNORM;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &shadow_output_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow output image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, shadow_output_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &shadow_output_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate shadow output memory\n");
            return false;
        }

        vkBindImageMemory(device_, shadow_output_image_, shadow_output_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = shadow_output_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &shadow_output_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow output view\n");
            return false;
        }

        printf("  Shadow output buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
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

        /* Set 1: Voxel objects (atlas sampler, metadata buffer) */
        VkDescriptorSetLayoutBinding vobj_bindings[2]{};
        vobj_bindings[0].binding = 0;
        vobj_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        vobj_bindings[0].descriptorCount = 1;
        vobj_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        vobj_bindings[1].binding = 1;
        vobj_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vobj_bindings[1].descriptorCount = 1;
        vobj_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo vobj_layout_info{};
        vobj_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        vobj_layout_info.bindingCount = 2;
        vobj_layout_info.pBindings = vobj_bindings;

        if (vkCreateDescriptorSetLayout(device_, &vobj_layout_info, nullptr, &gbuffer_compute_vobj_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create gbuffer compute vobj layout\n");
            return false;
        }

        /* Set 2: G-buffer output images (albedo, normal, material, depth, motion_vector) */
        VkDescriptorSetLayoutBinding output_bindings[5]{};
        for (int i = 0; i < 5; i++)
        {
            output_bindings[i].binding = i;
            output_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_bindings[i].descriptorCount = 1;
            output_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 5;
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
        /* Set 0: Terrain data for HDDA (voxel buffer, chunk headers, shadow volume) */
        VkDescriptorSetLayoutBinding input_bindings[3]{};
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

        VkDescriptorSetLayoutCreateInfo input_layout_info{};
        input_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        input_layout_info.bindingCount = 3;
        input_layout_info.pBindings = input_bindings;

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &shadow_compute_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create shadow compute input layout\n");
            return false;
        }

        /* Set 1: G-buffer samplers (depth, normal, blue noise) */
        VkDescriptorSetLayoutBinding gbuffer_bindings[3]{};
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

        VkDescriptorSetLayoutCreateInfo gbuffer_layout_info{};
        gbuffer_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        gbuffer_layout_info.bindingCount = 3;
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

        /* Pipeline layout */
        VkDescriptorSetLayout set_layouts[3] = {
            shadow_compute_input_layout_,
            shadow_compute_gbuffer_layout_,
            shadow_compute_output_layout_};

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
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 3; /* voxel data, headers, vobj metadata */
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; /* material palette, temporal ubo */
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 1; /* vobj atlas */
        pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[3].descriptorCount = MAX_FRAMES_IN_FLIGHT * 5; /* 5 G-buffer outputs (incl motion vector) */

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

            /* Set 1: Voxel objects */
            if (vobj_atlas_view_ && vobj_atlas_sampler_)
            {
                VkDescriptorImageInfo atlas_info{};
                atlas_info.sampler = vobj_atlas_sampler_;
                atlas_info.imageView = vobj_atlas_view_;
                atlas_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkDescriptorBufferInfo vobj_meta_info{};
                vobj_meta_info.buffer = vobj_metadata_buffer_.buffer;
                vobj_meta_info.offset = 0;
                vobj_meta_info.range = VK_WHOLE_SIZE;

                VkWriteDescriptorSet vobj_writes[2]{};
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

                vkUpdateDescriptorSets(device_, 2, vobj_writes, 0, nullptr);
            }

            /* Set 2: G-buffer output images (albedo, normal, material, depth, motion_vector) */
            VkDescriptorImageInfo gbuffer_infos[5]{};
            for (int g = 0; g < 4; g++)
            {
                gbuffer_infos[g].imageView = gbuffer_views_[g];
                gbuffer_infos[g].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            gbuffer_infos[4].imageView = motion_vector_view_;
            gbuffer_infos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_writes[5]{};
            for (int g = 0; g < 5; g++)
            {
                output_writes[g].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                output_writes[g].dstSet = gbuffer_compute_output_sets_[i];
                output_writes[g].dstBinding = g;
                output_writes[g].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                output_writes[g].descriptorCount = 1;
                output_writes[g].pImageInfo = &gbuffer_infos[g];
            }

            vkUpdateDescriptorSets(device_, 5, output_writes, 0, nullptr);
        }

        printf("  G-buffer compute descriptor sets created\n");
        return true;
    }

    bool Renderer::create_shadow_compute_descriptor_sets()
    {
        if (!compute_resources_initialized_ || voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return true;

        /* Create descriptor pool */
        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; /* voxel_data + chunk_headers */
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 4; /* depth, normal, blue noise, shadow_volume */
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 1;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 3;

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
            /* Set 0: Terrain data for HDDA (voxel buffer, chunk headers) */
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

            VkWriteDescriptorSet input_writes[3]{};
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

            vkUpdateDescriptorSets(device_, shadow_volume_view_ ? 3 : 2, input_writes, 0, nullptr);

            /* Set 1: G-buffer samplers (depth, normal, blue noise) */
            VkDescriptorImageInfo depth_info{};
            depth_info.sampler = gbuffer_sampler_;
            depth_info.imageView = gbuffer_views_[GBUFFER_LINEAR_DEPTH];
            depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normal_info{};
            normal_info.sampler = gbuffer_sampler_;
            normal_info.imageView = gbuffer_views_[GBUFFER_NORMAL];
            normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo noise_info{};
            noise_info.sampler = blue_noise_sampler_ ? blue_noise_sampler_ : gbuffer_sampler_;
            noise_info.imageView = blue_noise_view_ ? blue_noise_view_ : gbuffer_views_[0];
            noise_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet gbuffer_writes[3]{};
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
            gbuffer_writes[2].pImageInfo = &noise_info;

            vkUpdateDescriptorSets(device_, 3, gbuffer_writes, 0, nullptr);

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

    void Renderer::dispatch_gbuffer_compute(const VoxelVolume *vol, int32_t object_count)
    {
        if (!compute_resources_initialized_ || !gbuffer_compute_pipeline_ || !vol)
            return;

        terrain_draw_count_++;

        /* Cache volume parameters for deferred lighting */
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

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition G-buffer images to GENERAL for compute write */
        VkImageMemoryBarrier barriers[5]{};
        for (int i = 0; i < 4; i++)
        {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = gbuffer_images_[i];
            barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[i].subresourceRange.baseMipLevel = 0;
            barriers[i].subresourceRange.levelCount = 1;
            barriers[i].subresourceRange.baseArrayLayer = 0;
            barriers[i].subresourceRange.layerCount = 1;
            barriers[i].srcAccessMask = 0;
            barriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        }
        /* Motion vector image barrier */
        barriers[4].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[4].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[4].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[4].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[4].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[4].image = motion_vector_image_;
        barriers[4].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[4].subresourceRange.baseMipLevel = 0;
        barriers[4].subresourceRange.levelCount = 1;
        barriers[4].subresourceRange.baseArrayLayer = 0;
        barriers[4].subresourceRange.layerCount = 1;
        barriers[4].srcAccessMask = 0;
        barriers[4].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 5, barriers);

        /* Bind pipeline and descriptor sets */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gbuffer_compute_pipeline_);

        VkDescriptorSet sets[3] = {
            gbuffer_compute_terrain_sets_[current_frame_],
            gbuffer_compute_vobj_sets_[current_frame_],
            gbuffer_compute_output_sets_[current_frame_]};

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                gbuffer_compute_layout_, 0, 3, sets, 0, nullptr);

        /* Push constants */
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
        pc.object_count = object_count;

        vkCmdPushConstants(cmd, gbuffer_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition G-buffer images to COLOR_ATTACHMENT for voxel objects render pass */
        for (int i = 0; i < 4; i++)
        {
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[i].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        /* Motion vector image transition */
        barriers[4].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[4].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[4].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[4].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 5, barriers);

        gbuffer_compute_dispatched_ = true;
    }

    void Renderer::dispatch_shadow_compute()
    {
        if (!compute_resources_initialized_ || !shadow_compute_pipeline_ || !shadow_volume_view_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition shadow output to GENERAL for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = shadow_output_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Bind pipeline and descriptor sets */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_compute_pipeline_);

        VkDescriptorSet sets[3] = {
            shadow_compute_input_sets_[current_frame_],
            shadow_compute_gbuffer_sets_[current_frame_],
            shadow_compute_output_sets_[current_frame_]};

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                shadow_compute_layout_, 0, 3, sets, 0, nullptr);

        /* Push constants - same as gbuffer for consistency */
        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
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
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.object_count = 0;

        vkCmdPushConstants(cmd, shadow_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition shadow output to SHADER_READ_ONLY for sampling in lighting pass */
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void Renderer::dispatch_temporal_shadow_resolve()
    {
        if (!compute_resources_initialized_ || !temporal_compute_pipeline_ || !history_image_views_[0] || !history_image_views_[1])
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        const int write_index = history_write_index_ & 1;
        const int read_index = 1 - write_index;

        /* Transition write history image to GENERAL for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = history_images_[write_index];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Update history descriptors for this frame */
        VkDescriptorImageInfo shadow_history_info{};
        shadow_history_info.sampler = gbuffer_sampler_;
        shadow_history_info.imageView = history_image_views_[read_index];
        shadow_history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet history_write{};
        history_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        history_write.dstSet = temporal_shadow_input_sets_[current_frame_];
        history_write.dstBinding = 4;
        history_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        history_write.descriptorCount = 1;
        history_write.pImageInfo = &shadow_history_info;

        VkDescriptorImageInfo out_info{};
        out_info.imageView = history_image_views_[write_index];
        out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet out_write{};
        out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out_write.dstSet = temporal_shadow_output_sets_[current_frame_];
        out_write.dstBinding = 0;
        out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        out_write.descriptorCount = 1;
        out_write.pImageInfo = &out_info;

        VkWriteDescriptorSet writes[2] = {history_write, out_write};
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_compute_pipeline_);
        VkDescriptorSet sets[2] = {temporal_shadow_input_sets_[current_frame_], temporal_shadow_output_sets_[current_frame_]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_compute_layout_, 0, 2, sets, 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.rt_quality = rt_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.reserved[0] = temporal_shadow_history_valid_ ? 1 : 0;

        vkCmdPushConstants(cmd, temporal_compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition resolved image to SHADER_READ_ONLY for lighting pass sampling */
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Point deferred lighting at the resolved shadow image for this frame */
        update_deferred_shadow_buffer_descriptor(current_frame_, history_image_views_[write_index]);

        temporal_shadow_history_valid_ = true;
        history_write_index_ = read_index;
    }

    void Renderer::destroy_compute_raymarching_resources()
    {
        if (!compute_resources_initialized_)
            return;

        vkDeviceWaitIdle(device_);

        if (shadow_output_view_)
        {
            vkDestroyImageView(device_, shadow_output_view_, nullptr);
            shadow_output_view_ = VK_NULL_HANDLE;
        }
        if (shadow_output_image_)
        {
            vkDestroyImage(device_, shadow_output_image_, nullptr);
            shadow_output_image_ = VK_NULL_HANDLE;
        }
        if (shadow_output_memory_)
        {
            vkFreeMemory(device_, shadow_output_memory_, nullptr);
            shadow_output_memory_ = VK_NULL_HANDLE;
        }

        if (gbuffer_compute_pipeline_)
        {
            vkDestroyPipeline(device_, gbuffer_compute_pipeline_, nullptr);
            gbuffer_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, gbuffer_compute_layout_, nullptr);
            gbuffer_compute_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_terrain_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_compute_terrain_layout_, nullptr);
            gbuffer_compute_terrain_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_vobj_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_compute_vobj_layout_, nullptr);
            gbuffer_compute_vobj_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_compute_output_layout_, nullptr);
            gbuffer_compute_output_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, gbuffer_compute_descriptor_pool_, nullptr);
            gbuffer_compute_descriptor_pool_ = VK_NULL_HANDLE;
        }

        if (shadow_compute_pipeline_)
        {
            vkDestroyPipeline(device_, shadow_compute_pipeline_, nullptr);
            shadow_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, shadow_compute_layout_, nullptr);
            shadow_compute_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_input_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, shadow_compute_input_layout_, nullptr);
            shadow_compute_input_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_gbuffer_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, shadow_compute_gbuffer_layout_, nullptr);
            shadow_compute_gbuffer_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, shadow_compute_output_layout_, nullptr);
            shadow_compute_output_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, shadow_compute_descriptor_pool_, nullptr);
            shadow_compute_descriptor_pool_ = VK_NULL_HANDLE;
        }

        compute_resources_initialized_ = false;
    }

}
