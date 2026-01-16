#include "renderer.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    bool Renderer::create_ao_output_resources()
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

        if (vkCreateImage(device_, &image_info, nullptr, &ao_output_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create AO output image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, ao_output_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &ao_output_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate AO output memory\n");
            return false;
        }

        vkBindImageMemory(device_, ao_output_image_, ao_output_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = ao_output_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &ao_output_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create AO output view\n");
            return false;
        }

        printf("  AO output buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_ao_history_resources()
    {
        for (int i = 0; i < 2; i++)
        {
            if (ao_history_images_[i] || ao_history_image_views_[i] || ao_history_image_memory_[i])
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

            if (vkCreateImage(device_, &image_info, nullptr, &ao_history_images_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create AO history image %d\n", i);
                return false;
            }

            VkMemoryRequirements mem_reqs;
            vkGetImageMemoryRequirements(device_, ao_history_images_[i], &mem_reqs);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_reqs.size;
            alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device_, &alloc_info, nullptr, &ao_history_image_memory_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate AO history memory %d\n", i);
                return false;
            }

            vkBindImageMemory(device_, ao_history_images_[i], ao_history_image_memory_[i], 0);

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = ao_history_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = VK_FORMAT_R8_UNORM;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &ao_history_image_views_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create AO history view %d\n", i);
                return false;
            }
        }

        printf("  AO history buffers created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_ao_compute_pipeline()
    {
        /* Reuse shadow_compute_input_layout_ (set 0), shadow_compute_gbuffer_layout_ (set 1),
           and shadow_compute_output_layout_ (set 2) - same bindings work for AO */

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

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &ao_compute_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create AO compute pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_raymarch_ao_comp_spv,
                shaders::k_shader_raymarch_ao_comp_spv_size,
                ao_compute_layout_,
                &ao_compute_pipeline_))
        {
            fprintf(stderr, "Failed to create AO compute pipeline\n");
            return false;
        }

        printf("  AO compute pipeline created\n");
        return true;
    }

    bool Renderer::create_temporal_ao_pipeline()
    {
        /* Reuse temporal_shadow layouts for AO */

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

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &temporal_ao_compute_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create temporal AO pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_temporal_ao_comp_spv,
                shaders::k_shader_temporal_ao_comp_spv_size,
                temporal_ao_compute_layout_,
                &temporal_ao_compute_pipeline_))
        {
            fprintf(stderr, "Failed to create temporal AO compute pipeline\n");
            return false;
        }

        printf("  Temporal AO pipeline created\n");
        return true;
    }

    bool Renderer::create_ao_compute_descriptor_sets()
    {
        if (!compute_resources_initialized_ || voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return true;

        /* Create descriptor pool for AO */
        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 4;
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 1;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 3;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &ao_compute_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create AO compute descriptor pool\n");
            return false;
        }

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
        alloc_info.descriptorPool = ao_compute_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, ao_compute_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate AO compute input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = gbuffer_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, ao_compute_gbuffer_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate AO compute gbuffer sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, ao_compute_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate AO compute output sets\n");
            return false;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            /* Set 0: Input data (same as shadow) */
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
            input_writes[0].dstSet = ao_compute_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pBufferInfo = &voxel_data_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = ao_compute_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pBufferInfo = &headers_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = ao_compute_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &shadow_vol_info;

            vkUpdateDescriptorSets(device_, shadow_volume_view_ ? 3 : 2, input_writes, 0, nullptr);

            /* Set 1: G-buffer samplers */
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
            gbuffer_writes[0].dstSet = ao_compute_gbuffer_sets_[i];
            gbuffer_writes[0].dstBinding = 0;
            gbuffer_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[0].descriptorCount = 1;
            gbuffer_writes[0].pImageInfo = &depth_info;

            gbuffer_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbuffer_writes[1].dstSet = ao_compute_gbuffer_sets_[i];
            gbuffer_writes[1].dstBinding = 1;
            gbuffer_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[1].descriptorCount = 1;
            gbuffer_writes[1].pImageInfo = &normal_info;

            gbuffer_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbuffer_writes[2].dstSet = ao_compute_gbuffer_sets_[i];
            gbuffer_writes[2].dstBinding = 2;
            gbuffer_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_writes[2].descriptorCount = 1;
            gbuffer_writes[2].pImageInfo = &noise_info;

            vkUpdateDescriptorSets(device_, 3, gbuffer_writes, 0, nullptr);

            /* Set 2: AO output */
            VkDescriptorImageInfo output_info{};
            output_info.imageView = ao_output_view_;
            output_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_write{};
            output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_write.dstSet = ao_compute_output_sets_[i];
            output_write.dstBinding = 0;
            output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_write.descriptorCount = 1;
            output_write.pImageInfo = &output_info;

            vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
        }

        printf("  AO compute descriptor sets created\n");
        return true;
    }

    bool Renderer::create_temporal_ao_descriptor_sets()
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

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &temporal_ao_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create temporal AO descriptor pool\n");
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
        alloc_info.descriptorPool = temporal_ao_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, temporal_ao_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate temporal AO input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, temporal_ao_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate temporal AO output sets\n");
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

            VkDescriptorImageInfo ao_current_info{};
            ao_current_info.sampler = gbuffer_sampler_;
            ao_current_info.imageView = ao_output_view_ ? ao_output_view_ : gbuffer_views_[0];
            ao_current_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo ao_history_info{};
            ao_history_info.sampler = gbuffer_sampler_;
            ao_history_info.imageView = ao_history_image_views_[0] ? ao_history_image_views_[0] : gbuffer_views_[0];
            ao_history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet input_writes[5]{};
            input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[0].dstSet = temporal_ao_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pImageInfo = &depth_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = temporal_ao_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pImageInfo = &normal_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = temporal_ao_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &motion_info;

            input_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[3].dstSet = temporal_ao_input_sets_[i];
            input_writes[3].dstBinding = 3;
            input_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[3].descriptorCount = 1;
            input_writes[3].pImageInfo = &ao_current_info;

            input_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[4].dstSet = temporal_ao_input_sets_[i];
            input_writes[4].dstBinding = 4;
            input_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[4].descriptorCount = 1;
            input_writes[4].pImageInfo = &ao_history_info;

            vkUpdateDescriptorSets(device_, 5, input_writes, 0, nullptr);

            VkDescriptorImageInfo out_info{};
            out_info.imageView = ao_history_image_views_[0] ? ao_history_image_views_[0] : ao_output_view_;
            out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_write{};
            output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_write.dstSet = temporal_ao_output_sets_[i];
            output_write.dstBinding = 0;
            output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_write.descriptorCount = 1;
            output_write.pImageInfo = &out_info;

            vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
        }

        printf("  Temporal AO descriptor sets created\n");
        return true;
    }

    void Renderer::destroy_ao_resources()
    {
        vkDeviceWaitIdle(device_);

        if (ao_output_view_)
        {
            vkDestroyImageView(device_, ao_output_view_, nullptr);
            ao_output_view_ = VK_NULL_HANDLE;
        }
        if (ao_output_image_)
        {
            vkDestroyImage(device_, ao_output_image_, nullptr);
            ao_output_image_ = VK_NULL_HANDLE;
        }
        if (ao_output_memory_)
        {
            vkFreeMemory(device_, ao_output_memory_, nullptr);
            ao_output_memory_ = VK_NULL_HANDLE;
        }

        for (int i = 0; i < 2; i++)
        {
            if (ao_history_image_views_[i])
            {
                vkDestroyImageView(device_, ao_history_image_views_[i], nullptr);
                ao_history_image_views_[i] = VK_NULL_HANDLE;
            }
            if (ao_history_images_[i])
            {
                vkDestroyImage(device_, ao_history_images_[i], nullptr);
                ao_history_images_[i] = VK_NULL_HANDLE;
            }
            if (ao_history_image_memory_[i])
            {
                vkFreeMemory(device_, ao_history_image_memory_[i], nullptr);
                ao_history_image_memory_[i] = VK_NULL_HANDLE;
            }
        }

        if (ao_compute_pipeline_)
        {
            vkDestroyPipeline(device_, ao_compute_pipeline_, nullptr);
            ao_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (ao_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, ao_compute_layout_, nullptr);
            ao_compute_layout_ = VK_NULL_HANDLE;
        }
        if (ao_compute_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, ao_compute_descriptor_pool_, nullptr);
            ao_compute_descriptor_pool_ = VK_NULL_HANDLE;
        }

        if (temporal_ao_compute_pipeline_)
        {
            vkDestroyPipeline(device_, temporal_ao_compute_pipeline_, nullptr);
            temporal_ao_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (temporal_ao_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, temporal_ao_compute_layout_, nullptr);
            temporal_ao_compute_layout_ = VK_NULL_HANDLE;
        }
        if (temporal_ao_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, temporal_ao_descriptor_pool_, nullptr);
            temporal_ao_descriptor_pool_ = VK_NULL_HANDLE;
        }

        ao_resources_initialized_ = false;
    }

    void Renderer::update_ao_volume_descriptor()
    {
        if (!shadow_volume_view_ || !shadow_volume_sampler_ || !ao_compute_descriptor_pool_)
            return;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo shadow_vol_info{};
            shadow_vol_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadow_vol_info.imageView = shadow_volume_view_;
            shadow_vol_info.sampler = shadow_volume_sampler_;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = ao_compute_input_sets_[i];
            write.dstBinding = 2;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &shadow_vol_info;

            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

}
