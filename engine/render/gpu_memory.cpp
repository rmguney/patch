#define VMA_IMPLEMENTATION
#include "gpu_memory.h"
#include <cstdio>

namespace patch {

bool GpuAllocator::init(VkInstance instance, VkPhysicalDevice phys, VkDevice device, uint32_t api_version)
{
    VmaAllocatorCreateInfo ci{};
    ci.instance = instance;
    ci.physicalDevice = phys;
    ci.device = device;
    ci.vulkanApiVersion = api_version;

    VkResult result = vmaCreateAllocator(&ci, &allocator);
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "VMA: Failed to create allocator (VkResult %d)\n", result);
        return false;
    }
    return true;
}

void GpuAllocator::destroy()
{
    if (allocator)
    {
        dump_stats();
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
}

VkBuffer GpuAllocator::create_buffer(const VkBufferCreateInfo &buf_info,
                                     VmaAllocationCreateFlags flags,
                                     VkMemoryPropertyFlags required_flags,
                                     VmaAllocation *out_alloc,
                                     void **out_mapped)
{
    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = flags;
    alloc_ci.requiredFlags = required_flags;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    VkResult result = vmaCreateBuffer(allocator, &buf_info, &alloc_ci, &buffer, out_alloc, &info);
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "VMA: Buffer allocation failed (size=%llu, VkResult %d)\n",
                (unsigned long long)buf_info.size, result);
        *out_alloc = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    *out_mapped = info.pMappedData;
    return buffer;
}

VkImage GpuAllocator::create_image(const VkImageCreateInfo &img_info,
                                   VmaMemoryUsage usage,
                                   VmaAllocation *out_alloc)
{
    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = usage;

    VkImage image = VK_NULL_HANDLE;
    VkResult result = vmaCreateImage(allocator, &img_info, &alloc_ci, &image, out_alloc, nullptr);
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "VMA: Image allocation failed (VkResult %d)\n", result);
    }
    return image;
}

void GpuAllocator::destroy_buffer(VkBuffer buffer, VmaAllocation alloc)
{
    if (buffer || alloc)
    {
        vmaDestroyBuffer(allocator, buffer, alloc);
    }
}

void GpuAllocator::destroy_image(VkImage image, VmaAllocation alloc)
{
    if (image || alloc)
    {
        vmaDestroyImage(allocator, image, alloc);
    }
}

void GpuAllocator::dump_stats()
{
    if (!allocator) return;

    VmaTotalStatistics stats;
    vmaCalculateStatistics(allocator, &stats);

    printf("VMA Stats: %u allocs, %llu bytes used, %llu bytes total\n",
           stats.total.statistics.allocationCount,
           (unsigned long long)stats.total.statistics.allocationBytes,
           (unsigned long long)stats.total.statistics.blockBytes);
}

} // namespace patch
