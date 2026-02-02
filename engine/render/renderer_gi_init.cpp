#include "renderer.h"
#include "shaders_embedded.h"
#include <cstdio>

namespace patch
{

    bool Renderer::create_gi_radiance_resources()
    {
        gi_radiance_dims_[0] = 128;
        gi_radiance_dims_[1] = 64;
        gi_radiance_dims_[2] = 128;

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_3D;
        image_info.extent.width = gi_radiance_dims_[0];
        image_info.extent.height = gi_radiance_dims_[1];
        image_info.extent.depth = gi_radiance_dims_[2];
        image_info.mipLevels = GI_MIP_LEVELS;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        gi_radiance_image_ = gpu_allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &gi_radiance_memory_);
        if (gi_radiance_image_ == VK_NULL_HANDLE)
        {
            fprintf(stderr, "Failed to create GI radiance image\n");
            return false;
        }

        /* Full view (all mip levels) for sampling */
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = gi_radiance_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
        view_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = GI_MIP_LEVELS;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &gi_radiance_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI radiance view\n");
            return false;
        }

        /* Per-mip views for storage image writes */
        for (uint32_t mip = 0; mip < GI_MIP_LEVELS; mip++)
        {
            VkImageViewCreateInfo mip_view_info{};
            mip_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            mip_view_info.image = gi_radiance_image_;
            mip_view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
            mip_view_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            mip_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mip_view_info.subresourceRange.baseMipLevel = mip;
            mip_view_info.subresourceRange.levelCount = 1;
            mip_view_info.subresourceRange.baseArrayLayer = 0;
            mip_view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &mip_view_info, nullptr, &gi_radiance_mip_views_[mip]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create GI radiance mip view %u\n", mip);
                return false;
            }
        }

        /* Trilinear sampler for cone tracing */
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = static_cast<float>(GI_MIP_LEVELS - 1);

        if (vkCreateSampler(device_, &sampler_info, nullptr, &gi_radiance_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI radiance sampler\n");
            return false;
        }

        printf("  GI radiance volume created: %ux%ux%u (%u mips)\n",
               gi_radiance_dims_[0], gi_radiance_dims_[1], gi_radiance_dims_[2], GI_MIP_LEVELS);
        return true;
    }

    bool Renderer::create_gi_opacity_resources()
    {
        uint32_t opacity_w = gi_radiance_dims_[0] / 2;
        uint32_t opacity_h = gi_radiance_dims_[1] / 2;
        uint32_t opacity_d = gi_radiance_dims_[2] / 2;

        for (int dir = 0; dir < 6; dir++)
        {
            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_3D;
            image_info.extent.width = opacity_w;
            image_info.extent.height = opacity_h;
            image_info.extent.depth = opacity_d;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = VK_FORMAT_R8_UNORM;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;

            gi_opacity_images_[dir] = gpu_allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &gi_opacity_memory_[dir]);
            if (gi_opacity_images_[dir] == VK_NULL_HANDLE)
            {
                fprintf(stderr, "Failed to create GI opacity image dir=%d\n", dir);
                return false;
            }

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = gi_opacity_images_[dir];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
            view_info.format = VK_FORMAT_R8_UNORM;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &gi_opacity_views_[dir]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create GI opacity view dir=%d\n", dir);
                return false;
            }
        }

        /* Linear sampler for opacity interpolation */
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(device_, &sampler_info, nullptr, &gi_opacity_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI opacity sampler\n");
            return false;
        }

        printf("  GI opacity textures created: 6x %ux%ux%u\n", opacity_w, opacity_h, opacity_d);
        return true;
    }

    bool Renderer::create_gi_output_resources()
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = swapchain_extent_.width;
        image_info.extent.height = swapchain_extent_.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        gi_output_image_ = gpu_allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &gi_output_memory_);
        if (gi_output_image_ == VK_NULL_HANDLE)
        {
            fprintf(stderr, "Failed to create GI output image\n");
            return false;
        }

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = gi_output_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &gi_output_view_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI output view\n");
            return false;
        }

        printf("  GI output buffer created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_gi_history_resources()
    {
        for (int i = 0; i < 2; i++)
        {
            if (gi_history_images_[i] || gi_history_views_[i] || gi_history_memory_[i])
                continue;

            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.extent.width = swapchain_extent_.width;
            image_info.extent.height = swapchain_extent_.height;
            image_info.extent.depth = 1;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;

            gi_history_images_[i] = gpu_allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &gi_history_memory_[i]);
            if (gi_history_images_[i] == VK_NULL_HANDLE)
            {
                fprintf(stderr, "Failed to create GI history image %d\n", i);
                return false;
            }

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = gi_history_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &gi_history_views_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create GI history view %d\n", i);
                return false;
            }
        }

        printf("  GI history buffers created: %ux%u\n", swapchain_extent_.width, swapchain_extent_.height);
        return true;
    }

    bool Renderer::create_gi_inject_pipeline()
    {
        /* Set 1: 7 storage images (radiance + 6 opacity) */
        VkDescriptorSetLayoutBinding output_bindings[7]{};
        for (uint32_t b = 0; b < 7; b++)
        {
            output_bindings[b].binding = b;
            output_bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            output_bindings[b].descriptorCount = 1;
            output_bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 7;
        output_layout_info.pBindings = output_bindings;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &gi_inject_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI inject output layout\n");
            return false;
        }

        VkDescriptorSetLayout set_layouts[2] = {shadow_compute_input_layout_, gi_inject_output_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(GIInjectPushConstants);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gi_inject_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI inject pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_gi_inject_comp_spv,
                shaders::k_shader_gi_inject_comp_spv_size,
                gi_inject_layout_, &gi_inject_pipeline_))
        {
            fprintf(stderr, "Failed to create GI inject pipeline\n");
            return false;
        }

        printf("  GI inject pipeline created\n");
        return true;
    }

    bool Renderer::create_gi_mipmap_pipeline()
    {
        /* Set 0: 1 sampled image (radiance at source mip) */
        VkDescriptorSetLayoutBinding src_binding{};
        src_binding.binding = 0;
        src_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        src_binding.descriptorCount = 1;
        src_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo src_layout_info{};
        src_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        src_layout_info.bindingCount = 1;
        src_layout_info.pBindings = &src_binding;

        if (vkCreateDescriptorSetLayout(device_, &src_layout_info, nullptr, &gi_mipmap_src_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI mipmap src layout\n");
            return false;
        }

        /* Set 1: 1 storage image (radiance at dst mip) */
        VkDescriptorSetLayoutBinding dst_binding{};
        dst_binding.binding = 0;
        dst_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        dst_binding.descriptorCount = 1;
        dst_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dst_layout_info{};
        dst_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dst_layout_info.bindingCount = 1;
        dst_layout_info.pBindings = &dst_binding;

        if (vkCreateDescriptorSetLayout(device_, &dst_layout_info, nullptr, &gi_mipmap_dst_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI mipmap dst layout\n");
            return false;
        }

        VkDescriptorSetLayout set_layouts[2] = {gi_mipmap_src_layout_, gi_mipmap_dst_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(GIMipmapPushConstants);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gi_mipmap_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI mipmap pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_gi_mipmap_comp_spv,
                shaders::k_shader_gi_mipmap_comp_spv_size,
                gi_mipmap_layout_, &gi_mipmap_pipeline_))
        {
            fprintf(stderr, "Failed to create GI mipmap pipeline\n");
            return false;
        }

        printf("  GI mipmap pipeline created\n");
        return true;
    }

    bool Renderer::create_gi_cone_pipeline()
    {
        /* Set 0: 10 sampled images (world_pos, normal, albedo, radiance, 6 opacity) */
        VkDescriptorSetLayoutBinding input_bindings[10]{};
        for (uint32_t b = 0; b < 10; b++)
        {
            input_bindings[b].binding = b;
            input_bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_bindings[b].descriptorCount = 1;
            input_bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo input_layout_info{};
        input_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        input_layout_info.bindingCount = 10;
        input_layout_info.pBindings = input_bindings;

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &gi_cone_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI cone input layout\n");
            return false;
        }

        /* Set 1: 1 storage image (GI output) */
        VkDescriptorSetLayoutBinding output_binding{};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 1;
        output_layout_info.pBindings = &output_binding;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &gi_cone_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI cone output layout\n");
            return false;
        }

        VkDescriptorSetLayout set_layouts[2] = {gi_cone_input_layout_, gi_cone_output_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(GIConePushConstants);

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gi_cone_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI cone pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_cone_gi_comp_spv,
                shaders::k_shader_cone_gi_comp_spv_size,
                gi_cone_layout_, &gi_cone_pipeline_))
        {
            fprintf(stderr, "Failed to create GI cone pipeline\n");
            return false;
        }

        printf("  GI cone pipeline created\n");
        return true;
    }

    bool Renderer::create_gi_temporal_pipeline()
    {
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

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gi_temporal_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI temporal pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_temporal_gi_comp_spv,
                shaders::k_shader_temporal_gi_comp_spv_size,
                gi_temporal_layout_, &gi_temporal_pipeline_))
        {
            fprintf(stderr, "Failed to create GI temporal pipeline\n");
            return false;
        }

        printf("  GI temporal pipeline created\n");
        return true;
    }

    bool Renderer::create_gi_descriptor_sets()
    {
        if (voxel_data_buffer_.buffer == VK_NULL_HANDLE)
            return false;

        VkDescriptorPoolSize pool_sizes[4]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * (1 + (GI_MIP_LEVELS - 1) + 10 + 5);
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * (7 + (GI_MIP_LEVELS - 1) + 1 + 1);
        pool_sizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[3].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        uint32_t max_sets = MAX_FRAMES_IN_FLIGHT * (
            2 +                              /* inject: input + output */
            2 * (GI_MIP_LEVELS - 1) +        /* mipmap: src + dst per level */
            2 +                              /* cone: input + output */
            2                                /* temporal: input + output */
        );

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 4;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = max_sets;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &gi_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI descriptor pool\n");
            return false;
        }

        /* Allocate inject input sets (reuse shadow_compute_input_layout_) */
        {
            VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                layouts[i] = shadow_compute_input_layout_;

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = gi_descriptor_pool_;
            alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
            alloc.pSetLayouts = layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, gi_inject_input_sets_) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI inject input sets\n");
                return false;
            }
        }

        /* Allocate inject output sets */
        {
            VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                layouts[i] = gi_inject_output_layout_;

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = gi_descriptor_pool_;
            alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
            alloc.pSetLayouts = layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, gi_inject_output_sets_) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI inject output sets\n");
                return false;
            }
        }

        /* Allocate mipmap src/dst sets for each mip transition */
        for (uint32_t mip = 0; mip < GI_MIP_LEVELS - 1; mip++)
        {
            VkDescriptorSetLayout src_layouts[MAX_FRAMES_IN_FLIGHT];
            VkDescriptorSetLayout dst_layouts[MAX_FRAMES_IN_FLIGHT];
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                src_layouts[i] = gi_mipmap_src_layout_;
                dst_layouts[i] = gi_mipmap_dst_layout_;
            }

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = gi_descriptor_pool_;
            alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

            VkDescriptorSet src_sets[MAX_FRAMES_IN_FLIGHT];
            VkDescriptorSet dst_sets[MAX_FRAMES_IN_FLIGHT];

            alloc.pSetLayouts = src_layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, src_sets) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI mipmap src sets mip %u\n", mip);
                return false;
            }

            alloc.pSetLayouts = dst_layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, dst_sets) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI mipmap dst sets mip %u\n", mip);
                return false;
            }

            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                gi_mipmap_src_sets_[i][mip] = src_sets[i];
                gi_mipmap_dst_sets_[i][mip] = dst_sets[i];
            }
        }

        /* Allocate cone input/output sets */
        {
            VkDescriptorSetLayout in_layouts[MAX_FRAMES_IN_FLIGHT];
            VkDescriptorSetLayout out_layouts[MAX_FRAMES_IN_FLIGHT];
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                in_layouts[i] = gi_cone_input_layout_;
                out_layouts[i] = gi_cone_output_layout_;
            }

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = gi_descriptor_pool_;
            alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

            alloc.pSetLayouts = in_layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, gi_cone_input_sets_) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI cone input sets\n");
                return false;
            }

            alloc.pSetLayouts = out_layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, gi_cone_output_sets_) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI cone output sets\n");
                return false;
            }
        }

        /* Allocate temporal input/output sets (reuse temporal shadow layouts) */
        {
            VkDescriptorSetLayout in_layouts[MAX_FRAMES_IN_FLIGHT];
            VkDescriptorSetLayout out_layouts[MAX_FRAMES_IN_FLIGHT];
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                in_layouts[i] = temporal_shadow_input_layout_;
                out_layouts[i] = temporal_shadow_output_layout_;
            }

            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = gi_descriptor_pool_;
            alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

            alloc.pSetLayouts = in_layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, gi_temporal_input_sets_) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI temporal input sets\n");
                return false;
            }

            alloc.pSetLayouts = out_layouts;
            if (vkAllocateDescriptorSets(device_, &alloc, gi_temporal_output_sets_) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI temporal output sets\n");
                return false;
            }
        }

        /* Write descriptors */
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            /* --- Inject input (same bindings as shadow compute) --- */
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
            shadow_vol_info.imageView = shadow_volume_view_ ? shadow_volume_view_ : gbuffer_views_[0];
            shadow_vol_info.sampler = shadow_volume_sampler_ ? shadow_volume_sampler_ : gbuffer_sampler_;

            VkDescriptorBufferInfo material_info{};
            material_info.buffer = voxel_material_buffer_.buffer;
            material_info.offset = 0;
            material_info.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet inject_input_writes[4]{};
            inject_input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            inject_input_writes[0].dstSet = gi_inject_input_sets_[i];
            inject_input_writes[0].dstBinding = 0;
            inject_input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            inject_input_writes[0].descriptorCount = 1;
            inject_input_writes[0].pBufferInfo = &voxel_data_info;

            inject_input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            inject_input_writes[1].dstSet = gi_inject_input_sets_[i];
            inject_input_writes[1].dstBinding = 1;
            inject_input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            inject_input_writes[1].descriptorCount = 1;
            inject_input_writes[1].pBufferInfo = &headers_info;

            inject_input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            inject_input_writes[2].dstSet = gi_inject_input_sets_[i];
            inject_input_writes[2].dstBinding = 2;
            inject_input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            inject_input_writes[2].descriptorCount = 1;
            inject_input_writes[2].pImageInfo = &shadow_vol_info;

            inject_input_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            inject_input_writes[3].dstSet = gi_inject_input_sets_[i];
            inject_input_writes[3].dstBinding = 3;
            inject_input_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            inject_input_writes[3].descriptorCount = 1;
            inject_input_writes[3].pBufferInfo = &material_info;

            vkUpdateDescriptorSets(device_, 4, inject_input_writes, 0, nullptr);

            /* --- Inject output: radiance mip0 + 6 opacity --- */
            VkDescriptorImageInfo inject_out_infos[7]{};
            inject_out_infos[0].imageView = gi_radiance_mip_views_[0];
            inject_out_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            for (int d = 0; d < 6; d++)
            {
                inject_out_infos[d + 1].imageView = gi_opacity_views_[d];
                inject_out_infos[d + 1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }

            VkWriteDescriptorSet inject_out_writes[7]{};
            for (int b = 0; b < 7; b++)
            {
                inject_out_writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                inject_out_writes[b].dstSet = gi_inject_output_sets_[i];
                inject_out_writes[b].dstBinding = b;
                inject_out_writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                inject_out_writes[b].descriptorCount = 1;
                inject_out_writes[b].pImageInfo = &inject_out_infos[b];
            }

            vkUpdateDescriptorSets(device_, 7, inject_out_writes, 0, nullptr);

            /* --- Mipmap src/dst sets for each mip transition (radiance only) --- */
            for (uint32_t mip = 0; mip < GI_MIP_LEVELS - 1; mip++)
            {
                VkImageView src_radiance_view = gi_radiance_mip_views_[mip];
                VkImageView dst_radiance_view = gi_radiance_mip_views_[mip + 1];

                VkDescriptorImageInfo src_info{};
                src_info.sampler = gi_radiance_sampler_;
                src_info.imageView = src_radiance_view;
                src_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet src_write{};
                src_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                src_write.dstSet = gi_mipmap_src_sets_[i][mip];
                src_write.dstBinding = 0;
                src_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                src_write.descriptorCount = 1;
                src_write.pImageInfo = &src_info;

                vkUpdateDescriptorSets(device_, 1, &src_write, 0, nullptr);

                VkDescriptorImageInfo dst_info{};
                dst_info.imageView = dst_radiance_view;
                dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkWriteDescriptorSet dst_write{};
                dst_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                dst_write.dstSet = gi_mipmap_dst_sets_[i][mip];
                dst_write.dstBinding = 0;
                dst_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                dst_write.descriptorCount = 1;
                dst_write.pImageInfo = &dst_info;

                vkUpdateDescriptorSets(device_, 1, &dst_write, 0, nullptr);
            }

            /* --- Cone input: G-buffer + radiance + 6 opacity --- */
            VkDescriptorImageInfo cone_in_infos[10]{};
            cone_in_infos[0].sampler = gbuffer_sampler_;
            cone_in_infos[0].imageView = gbuffer_views_[GBUFFER_WORLD_POS];
            cone_in_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            cone_in_infos[1].sampler = gbuffer_sampler_;
            cone_in_infos[1].imageView = gbuffer_views_[GBUFFER_NORMAL];
            cone_in_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            cone_in_infos[2].sampler = gbuffer_sampler_;
            cone_in_infos[2].imageView = gbuffer_views_[GBUFFER_ALBEDO];
            cone_in_infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            cone_in_infos[3].sampler = gi_radiance_sampler_;
            cone_in_infos[3].imageView = gi_radiance_view_;
            cone_in_infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            for (int d = 0; d < 6; d++)
            {
                cone_in_infos[4 + d].sampler = gi_opacity_sampler_;
                cone_in_infos[4 + d].imageView = gi_opacity_views_[d];
                cone_in_infos[4 + d].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet cone_in_writes[10]{};
            for (int b = 0; b < 10; b++)
            {
                cone_in_writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                cone_in_writes[b].dstSet = gi_cone_input_sets_[i];
                cone_in_writes[b].dstBinding = b;
                cone_in_writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                cone_in_writes[b].descriptorCount = 1;
                cone_in_writes[b].pImageInfo = &cone_in_infos[b];
            }

            vkUpdateDescriptorSets(device_, 10, cone_in_writes, 0, nullptr);

            /* --- Cone output --- */
            VkDescriptorImageInfo cone_out_info{};
            cone_out_info.imageView = gi_output_view_;
            cone_out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet cone_out_write{};
            cone_out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cone_out_write.dstSet = gi_cone_output_sets_[i];
            cone_out_write.dstBinding = 0;
            cone_out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            cone_out_write.descriptorCount = 1;
            cone_out_write.pImageInfo = &cone_out_info;

            vkUpdateDescriptorSets(device_, 1, &cone_out_write, 0, nullptr);

            /* --- Temporal input: depth, normal, motion, current GI, history GI --- */
            VkDescriptorImageInfo temporal_in_infos[5]{};
            temporal_in_infos[0].sampler = gbuffer_sampler_;
            temporal_in_infos[0].imageView = gbuffer_views_[GBUFFER_LINEAR_DEPTH];
            temporal_in_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            temporal_in_infos[1].sampler = gbuffer_sampler_;
            temporal_in_infos[1].imageView = gbuffer_views_[GBUFFER_NORMAL];
            temporal_in_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            temporal_in_infos[2].sampler = gbuffer_sampler_;
            temporal_in_infos[2].imageView = motion_vector_view_ ? motion_vector_view_ : gbuffer_views_[0];
            temporal_in_infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            temporal_in_infos[3].sampler = gbuffer_sampler_;
            temporal_in_infos[3].imageView = gi_output_view_ ? gi_output_view_ : gbuffer_views_[0];
            temporal_in_infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            temporal_in_infos[4].sampler = gbuffer_sampler_;
            temporal_in_infos[4].imageView = gi_history_views_[0] ? gi_history_views_[0] : gbuffer_views_[0];
            temporal_in_infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet temporal_in_writes[5]{};
            for (int b = 0; b < 5; b++)
            {
                temporal_in_writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                temporal_in_writes[b].dstSet = gi_temporal_input_sets_[i];
                temporal_in_writes[b].dstBinding = b;
                temporal_in_writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                temporal_in_writes[b].descriptorCount = 1;
                temporal_in_writes[b].pImageInfo = &temporal_in_infos[b];
            }

            vkUpdateDescriptorSets(device_, 5, temporal_in_writes, 0, nullptr);

            /* --- Temporal output --- */
            VkDescriptorImageInfo temporal_out_info{};
            temporal_out_info.imageView = gi_history_views_[0] ? gi_history_views_[0] : gi_output_view_;
            temporal_out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet temporal_out_write{};
            temporal_out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            temporal_out_write.dstSet = gi_temporal_output_sets_[i];
            temporal_out_write.dstBinding = 0;
            temporal_out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            temporal_out_write.descriptorCount = 1;
            temporal_out_write.pImageInfo = &temporal_out_info;

            vkUpdateDescriptorSets(device_, 1, &temporal_out_write, 0, nullptr);
        }

        printf("  GI descriptor sets created\n");
        return true;
    }

    void Renderer::update_deferred_gi_buffer_descriptor(uint32_t frame_index, VkImageView gi_view)
    {
        if (!deferred_lighting_descriptor_pool_ || !gi_view)
            return;

        VkDescriptorImageInfo gi_info{};
        gi_info.sampler = gbuffer_sampler_;
        gi_info.imageView = gi_view;
        gi_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = deferred_lighting_descriptor_sets_[frame_index];
        write.dstBinding = 8;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &gi_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    void Renderer::destroy_gi_resources()
    {
        vkDeviceWaitIdle(device_);

        if (gi_radiance_sampler_)
        {
            vkDestroySampler(device_, gi_radiance_sampler_, nullptr);
            gi_radiance_sampler_ = VK_NULL_HANDLE;
        }

        for (uint32_t mip = 0; mip < GI_MIP_LEVELS; mip++)
        {
            if (gi_radiance_mip_views_[mip])
            {
                vkDestroyImageView(device_, gi_radiance_mip_views_[mip], nullptr);
                gi_radiance_mip_views_[mip] = VK_NULL_HANDLE;
            }
        }

        if (gi_radiance_view_)
        {
            vkDestroyImageView(device_, gi_radiance_view_, nullptr);
            gi_radiance_view_ = VK_NULL_HANDLE;
        }
        if (gi_radiance_image_)
        {
            gpu_allocator_.destroy_image(gi_radiance_image_, gi_radiance_memory_);
            gi_radiance_image_ = VK_NULL_HANDLE;
            gi_radiance_memory_ = VK_NULL_HANDLE;
        }

        if (gi_opacity_sampler_)
        {
            vkDestroySampler(device_, gi_opacity_sampler_, nullptr);
            gi_opacity_sampler_ = VK_NULL_HANDLE;
        }

        for (int dir = 0; dir < 6; dir++)
        {
            if (gi_opacity_views_[dir])
            {
                vkDestroyImageView(device_, gi_opacity_views_[dir], nullptr);
                gi_opacity_views_[dir] = VK_NULL_HANDLE;
            }
            if (gi_opacity_images_[dir])
            {
                gpu_allocator_.destroy_image(gi_opacity_images_[dir], gi_opacity_memory_[dir]);
                gi_opacity_images_[dir] = VK_NULL_HANDLE;
                gi_opacity_memory_[dir] = VK_NULL_HANDLE;
            }
        }

        if (gi_output_view_)
        {
            vkDestroyImageView(device_, gi_output_view_, nullptr);
            gi_output_view_ = VK_NULL_HANDLE;
        }
        if (gi_output_image_)
        {
            gpu_allocator_.destroy_image(gi_output_image_, gi_output_memory_);
            gi_output_image_ = VK_NULL_HANDLE;
            gi_output_memory_ = VK_NULL_HANDLE;
        }

        for (int i = 0; i < 2; i++)
        {
            if (gi_history_views_[i])
            {
                vkDestroyImageView(device_, gi_history_views_[i], nullptr);
                gi_history_views_[i] = VK_NULL_HANDLE;
            }
            if (gi_history_images_[i])
            {
                gpu_allocator_.destroy_image(gi_history_images_[i], gi_history_memory_[i]);
                gi_history_images_[i] = VK_NULL_HANDLE;
                gi_history_memory_[i] = VK_NULL_HANDLE;
            }
        }

        if (gi_inject_pipeline_)
        {
            vkDestroyPipeline(device_, gi_inject_pipeline_, nullptr);
            gi_inject_pipeline_ = VK_NULL_HANDLE;
        }
        if (gi_inject_layout_)
        {
            vkDestroyPipelineLayout(device_, gi_inject_layout_, nullptr);
            gi_inject_layout_ = VK_NULL_HANDLE;
        }
        if (gi_mipmap_pipeline_)
        {
            vkDestroyPipeline(device_, gi_mipmap_pipeline_, nullptr);
            gi_mipmap_pipeline_ = VK_NULL_HANDLE;
        }
        if (gi_mipmap_layout_)
        {
            vkDestroyPipelineLayout(device_, gi_mipmap_layout_, nullptr);
            gi_mipmap_layout_ = VK_NULL_HANDLE;
        }
        if (gi_cone_pipeline_)
        {
            vkDestroyPipeline(device_, gi_cone_pipeline_, nullptr);
            gi_cone_pipeline_ = VK_NULL_HANDLE;
        }
        if (gi_cone_layout_)
        {
            vkDestroyPipelineLayout(device_, gi_cone_layout_, nullptr);
            gi_cone_layout_ = VK_NULL_HANDLE;
        }
        if (gi_temporal_pipeline_)
        {
            vkDestroyPipeline(device_, gi_temporal_pipeline_, nullptr);
            gi_temporal_pipeline_ = VK_NULL_HANDLE;
        }
        if (gi_temporal_layout_)
        {
            vkDestroyPipelineLayout(device_, gi_temporal_layout_, nullptr);
            gi_temporal_layout_ = VK_NULL_HANDLE;
        }

        if (gi_inject_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_inject_output_layout_, nullptr);
            gi_inject_output_layout_ = VK_NULL_HANDLE;
        }
        if (gi_mipmap_src_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_mipmap_src_layout_, nullptr);
            gi_mipmap_src_layout_ = VK_NULL_HANDLE;
        }
        if (gi_mipmap_dst_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_mipmap_dst_layout_, nullptr);
            gi_mipmap_dst_layout_ = VK_NULL_HANDLE;
        }
        if (gi_cone_input_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_cone_input_layout_, nullptr);
            gi_cone_input_layout_ = VK_NULL_HANDLE;
        }
        if (gi_cone_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_cone_output_layout_, nullptr);
            gi_cone_output_layout_ = VK_NULL_HANDLE;
        }

        if (gi_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, gi_descriptor_pool_, nullptr);
            gi_descriptor_pool_ = VK_NULL_HANDLE;
        }

        gi_resources_initialized_ = false;
        temporal_gi_history_valid_ = false;
        gi_history_write_index_ = 0;
    }

}
