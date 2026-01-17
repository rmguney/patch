#include "renderer.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    bool Renderer::create_lit_color_resources()
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = swapchain_extent_.width;
        image_info.extent.height = swapchain_extent_.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = swapchain_format_;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &lit_color_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create lit color image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, lit_color_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &lit_color_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate lit color memory\n");
            return false;
        }

        vkBindImageMemory(device_, lit_color_image_, lit_color_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = lit_color_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &lit_color_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create lit color view\n");
            return false;
        }

        printf("  Lit color buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_denoised_color_resources()
    {
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
        image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &denoised_color_image_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create denoised color image\n");
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, denoised_color_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &denoised_color_memory_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate denoised color memory\n");
            return false;
        }

        vkBindImageMemory(device_, denoised_color_image_, denoised_color_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = denoised_color_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &denoised_color_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create denoised color view\n");
            return false;
        }

        printf("  Denoised color buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_spatial_denoise_pipeline()
    {
        /* Input layout (set 0): depth, normal, lit_color samplers */
        VkDescriptorSetLayoutBinding input_bindings[3]{};
        input_bindings[0].binding = 0;
        input_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        input_bindings[0].descriptorCount = 1;
        input_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        input_bindings[1].binding = 1;
        input_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &spatial_denoise_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create spatial denoise input layout\n");
            return false;
        }

        /* Output layout (set 1): denoised image storage */
        VkDescriptorSetLayoutBinding output_binding{};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 1;
        output_layout_info.pBindings = &output_binding;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &spatial_denoise_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create spatial denoise output layout\n");
            return false;
        }

        VkDescriptorSetLayout set_layouts[2] = {spatial_denoise_input_layout_, spatial_denoise_output_layout_};

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

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &spatial_denoise_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create spatial denoise pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_spatial_denoise_comp_spv,
                shaders::k_shader_spatial_denoise_comp_spv_size,
                spatial_denoise_layout_,
                &spatial_denoise_pipeline_))
        {
            fprintf(stderr, "Failed to create spatial denoise compute pipeline\n");
            return false;
        }

        printf("  Spatial denoise pipeline created\n");
        return true;
    }

    bool Renderer::create_spatial_denoise_descriptor_sets()
    {
        if (!gbuffer_initialized_ || !lit_color_view_ || !denoised_color_view_)
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

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &spatial_denoise_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create spatial denoise descriptor pool\n");
            return false;
        }

        VkDescriptorSetLayout input_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout output_layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            input_layouts[i] = spatial_denoise_input_layout_;
            output_layouts[i] = spatial_denoise_output_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = spatial_denoise_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, spatial_denoise_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate spatial denoise input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, spatial_denoise_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate spatial denoise output sets\n");
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

            VkDescriptorImageInfo lit_color_info{};
            lit_color_info.sampler = gbuffer_sampler_;
            lit_color_info.imageView = lit_color_view_;
            lit_color_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet input_writes[3]{};
            input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[0].dstSet = spatial_denoise_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pImageInfo = &depth_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = spatial_denoise_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pImageInfo = &normal_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = spatial_denoise_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &lit_color_info;

            vkUpdateDescriptorSets(device_, 3, input_writes, 0, nullptr);

            VkDescriptorImageInfo output_info{};
            output_info.imageView = denoised_color_view_;
            output_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet output_write{};
            output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            output_write.dstSet = spatial_denoise_output_sets_[i];
            output_write.dstBinding = 0;
            output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_write.descriptorCount = 1;
            output_write.pImageInfo = &output_info;

            vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
        }

        printf("  Spatial denoise descriptor sets created\n");
        return true;
    }

    bool Renderer::create_deferred_lighting_intermediate_fb()
    {
        if (!lit_color_view_ || !depth_image_view_)
            return true;

        VkImageView attachments[2] = {lit_color_view_, depth_image_view_};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments = attachments;
        fb_info.width = swapchain_extent_.width;
        fb_info.height = swapchain_extent_.height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(device_, &fb_info, nullptr, &deferred_lighting_intermediate_fb_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create deferred lighting intermediate framebuffer\n");
            return false;
        }

        printf("  Deferred lighting intermediate framebuffer created\n");
        return true;
    }

    void Renderer::destroy_spatial_denoise_resources()
    {
        vkDeviceWaitIdle(device_);

        if (deferred_lighting_intermediate_fb_)
        {
            vkDestroyFramebuffer(device_, deferred_lighting_intermediate_fb_, nullptr);
            deferred_lighting_intermediate_fb_ = VK_NULL_HANDLE;
        }

        if (lit_color_view_)
        {
            vkDestroyImageView(device_, lit_color_view_, nullptr);
            lit_color_view_ = VK_NULL_HANDLE;
        }
        if (lit_color_image_)
        {
            vkDestroyImage(device_, lit_color_image_, nullptr);
            lit_color_image_ = VK_NULL_HANDLE;
        }
        if (lit_color_memory_)
        {
            vkFreeMemory(device_, lit_color_memory_, nullptr);
            lit_color_memory_ = VK_NULL_HANDLE;
        }

        if (denoised_color_view_)
        {
            vkDestroyImageView(device_, denoised_color_view_, nullptr);
            denoised_color_view_ = VK_NULL_HANDLE;
        }
        if (denoised_color_image_)
        {
            vkDestroyImage(device_, denoised_color_image_, nullptr);
            denoised_color_image_ = VK_NULL_HANDLE;
        }
        if (denoised_color_memory_)
        {
            vkFreeMemory(device_, denoised_color_memory_, nullptr);
            denoised_color_memory_ = VK_NULL_HANDLE;
        }

        if (spatial_denoise_pipeline_)
        {
            vkDestroyPipeline(device_, spatial_denoise_pipeline_, nullptr);
            spatial_denoise_pipeline_ = VK_NULL_HANDLE;
        }
        if (spatial_denoise_layout_)
        {
            vkDestroyPipelineLayout(device_, spatial_denoise_layout_, nullptr);
            spatial_denoise_layout_ = VK_NULL_HANDLE;
        }
        if (spatial_denoise_input_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, spatial_denoise_input_layout_, nullptr);
            spatial_denoise_input_layout_ = VK_NULL_HANDLE;
        }
        if (spatial_denoise_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, spatial_denoise_output_layout_, nullptr);
            spatial_denoise_output_layout_ = VK_NULL_HANDLE;
        }
        if (spatial_denoise_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, spatial_denoise_descriptor_pool_, nullptr);
            spatial_denoise_descriptor_pool_ = VK_NULL_HANDLE;
        }

        spatial_denoise_initialized_ = false;
    }

    void Renderer::set_denoise_quality(int level)
    {
        denoise_quality_ = level < 0 ? 0 : (level > 1 ? 1 : level);
    }

}
