#ifndef PATCH_ENGINE_GPU_BUFFER_H
#define PATCH_ENGINE_GPU_BUFFER_H

#include <vulkan/vulkan.h>
#include <cstdint>

namespace patch {

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct MeshBuffers {
    VulkanBuffer vertex;
    VulkanBuffer index;
    uint32_t index_count;
};

void create_buffer(VkDevice device, VkPhysicalDevice physical_device,
                   VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties, VulkanBuffer* buffer);

void destroy_buffer(VkDevice device, VulkanBuffer* buffer);

uint32_t find_memory_type(VkPhysicalDevice physical_device, 
                          uint32_t type_filter, VkMemoryPropertyFlags properties);

void create_sphere_mesh(VkDevice device, VkPhysicalDevice physical_device,
                        int sectors, int stacks, MeshBuffers* mesh);

void create_quad_mesh(VkDevice device, VkPhysicalDevice physical_device, MeshBuffers* mesh);

void create_box_mesh(VkDevice device, VkPhysicalDevice physical_device, MeshBuffers* mesh);

void destroy_mesh(VkDevice device, MeshBuffers* mesh);

}

#endif
