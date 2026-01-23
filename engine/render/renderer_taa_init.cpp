#include "renderer.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    bool Renderer::create_taa_history_resources()
    {
        for (int i = 0; i < 2; i++)
        {
            if (taa_history_images_[i] || taa_history_views_[i] || taa_history_memory_[i])
                continue;

            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.extent.width = swapchain_extent_.width;
            image_info.extent.height = swapchain_extent_.height;
            image_info.extent.depth = 1;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;

            taa_history_images_[i] = gpu_allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &taa_history_memory_[i]);
            if (!taa_history_images_[i])
            {
                fprintf(stderr, "Failed to create TAA history image %d\n", i);
                return false;
            }

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = taa_history_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &taa_history_views_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create TAA history view %d\n", i);
                return false;
            }
        }

        printf("  TAA history buffers created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_taa_pipeline()
    {
        /* Set 0: input samplers - current_color, history_color, motion_vectors */
        VkDescriptorSetLayoutBinding input_bindings[3]{};
        for (uint32_t b = 0; b < 3; b++)
        {
            input_bindings[b].binding = b;
            input_bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_bindings[b].descriptorCount = 1;
            input_bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo input_layout_info{};
        input_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        input_layout_info.bindingCount = 3;
        input_layout_info.pBindings = input_bindings;

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &taa_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create TAA input layout\n");
            return false;
        }

        /* Set 1: resolved output */
        VkDescriptorSetLayoutBinding output_binding{};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 1;
        output_layout_info.pBindings = &output_binding;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &taa_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create TAA output layout\n");
            return false;
        }

        VkDescriptorSetLayout set_layouts[2] = {taa_input_layout_, taa_output_layout_};

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

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &taa_compute_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create TAA pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_taa_resolve_comp_spv,
                shaders::k_shader_taa_resolve_comp_spv_size,
                taa_compute_layout_,
                &taa_compute_pipeline_))
        {
            fprintf(stderr, "Failed to create TAA compute pipeline\n");
            return false;
        }

        printf("  TAA pipeline created\n");
        return true;
    }

    bool Renderer::create_taa_descriptor_sets()
    {
        if (!lit_color_view_ || !motion_vector_view_ || !taa_history_views_[0] || !taa_history_views_[1])
            return true;

        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 3;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 2;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &taa_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create TAA descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout input_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout output_layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            input_layouts[i] = taa_input_layout_;
            output_layouts[i] = taa_output_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = taa_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, taa_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate TAA input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, taa_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate TAA output sets\n");
            return false;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo current_info{};
            current_info.sampler = gbuffer_sampler_;
            current_info.imageView = lit_color_view_;
            current_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo history_info{};
            history_info.sampler = gbuffer_sampler_;
            history_info.imageView = taa_history_views_[0];
            history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo motion_info{};
            motion_info.sampler = gbuffer_sampler_;
            motion_info.imageView = motion_vector_view_;
            motion_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet input_writes[3]{};
            input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[0].dstSet = taa_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pImageInfo = &current_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = taa_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pImageInfo = &history_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = taa_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &motion_info;

            vkUpdateDescriptorSets(device_, 3, input_writes, 0, nullptr);

            VkDescriptorImageInfo out_info{};
            out_info.imageView = taa_history_views_[0];
            out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_write{};
            output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_write.dstSet = taa_output_sets_[i];
            output_write.dstBinding = 0;
            output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_write.descriptorCount = 1;
            output_write.pImageInfo = &out_info;

            vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
        }

        taa_initialized_ = true;
        printf("  TAA descriptor sets created\n");
        return true;
    }

    void Renderer::destroy_taa_resources()
    {
        vkDeviceWaitIdle(device_);

        for (int i = 0; i < 2; i++)
        {
            if (taa_history_views_[i])
            {
                vkDestroyImageView(device_, taa_history_views_[i], nullptr);
                taa_history_views_[i] = VK_NULL_HANDLE;
            }
            if (taa_history_images_[i])
            {
                gpu_allocator_.destroy_image(taa_history_images_[i], taa_history_memory_[i]);
                taa_history_images_[i] = VK_NULL_HANDLE;
                taa_history_memory_[i] = VK_NULL_HANDLE;
            }
        }

        if (taa_compute_pipeline_)
        {
            vkDestroyPipeline(device_, taa_compute_pipeline_, nullptr);
            taa_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (taa_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, taa_compute_layout_, nullptr);
            taa_compute_layout_ = VK_NULL_HANDLE;
        }
        if (taa_input_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, taa_input_layout_, nullptr);
            taa_input_layout_ = VK_NULL_HANDLE;
        }
        if (taa_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, taa_output_layout_, nullptr);
            taa_output_layout_ = VK_NULL_HANDLE;
        }
        if (taa_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, taa_descriptor_pool_, nullptr);
            taa_descriptor_pool_ = VK_NULL_HANDLE;
        }

        taa_history_valid_ = false;
        taa_history_write_index_ = 0;
        taa_initialized_ = false;
    }

    void Renderer::set_taa_quality(int level)
    {
        taa_quality_ = level < 0 ? 0 : (level > 1 ? 1 : level);
    }

}
