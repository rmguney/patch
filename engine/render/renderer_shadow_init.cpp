#include "renderer.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

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

}
