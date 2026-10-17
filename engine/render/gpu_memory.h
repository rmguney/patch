#ifndef PATCH_ENGINE_GPU_MEMORY_H
#define PATCH_ENGINE_GPU_MEMORY_H

#include <vulkan/vulkan.h>
/* Third-party header: silence its warnings under clang-cl and plain MSVC cl alike */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "vk_mem_alloc.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace patch {

struct GpuAllocator {
    VmaAllocator allocator = VK_NULL_HANDLE;

    bool init(VkInstance instance, VkPhysicalDevice phys, VkDevice device, uint32_t api_version);
    void destroy();

    /* required_flags: memory properties the allocation must have. *out_mapped receives
     * the persistent mapping (null unless flags include VMA_ALLOCATION_CREATE_MAPPED_BIT). */
    VkBuffer create_buffer(const VkBufferCreateInfo &buf_info,
                           VmaAllocationCreateFlags flags,
                           VkMemoryPropertyFlags required_flags,
                           VmaAllocation *out_alloc,
                           void **out_mapped);

    VkImage create_image(const VkImageCreateInfo &img_info,
                         VmaMemoryUsage usage,
                         VmaAllocation *out_alloc);

    void destroy_buffer(VkBuffer buffer, VmaAllocation alloc);
    void destroy_image(VkImage image, VmaAllocation alloc);

    void dump_stats();
};

} // namespace patch

#endif
