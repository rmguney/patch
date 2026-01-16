#include "renderer.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <cmath>

namespace patch
{

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

}
