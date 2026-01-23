#ifndef PATCH_ENGINE_GPU_MEMORY_H
#define PATCH_ENGINE_GPU_MEMORY_H

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

namespace patch {

struct GpuAllocator {
    VmaAllocator allocator = VK_NULL_HANDLE;

    bool init(VkInstance instance, VkPhysicalDevice phys, VkDevice device, uint32_t api_version);
    void destroy();

    VkBuffer create_buffer(const VkBufferCreateInfo &buf_info,
                           VmaMemoryUsage usage,
                           VmaAllocationCreateFlags flags,
                           VmaAllocation *out_alloc);

    VkImage create_image(const VkImageCreateInfo &img_info,
                         VmaMemoryUsage usage,
                         VmaAllocation *out_alloc);

    void destroy_buffer(VkBuffer buffer, VmaAllocation alloc);
    void destroy_image(VkImage image, VmaAllocation alloc);

    void *map(VmaAllocation alloc);
    void unmap(VmaAllocation alloc);

    void dump_stats();
};

} // namespace patch

#endif
