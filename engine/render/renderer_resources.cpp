#include "renderer.h"
#include <cstring>
#include <vector>

namespace patch
{

    uint32_t Renderer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
        {
            if ((type_filter & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        return UINT32_MAX;
    }

    void Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties, VulkanBuffer *buffer)
    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device_, &buffer_info, nullptr, &buffer->buffer);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(device_, buffer->buffer, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, properties);

        vkAllocateMemory(device_, &alloc_info, nullptr, &buffer->memory);
        vkBindBufferMemory(device_, buffer->buffer, buffer->memory, 0);
    }

    void Renderer::destroy_buffer(VulkanBuffer *buffer)
    {
        if (!buffer)
            return;
        if (buffer->buffer)
            vkDestroyBuffer(device_, buffer->buffer, nullptr);
        if (buffer->memory)
            vkFreeMemory(device_, buffer->memory, nullptr);
        buffer->buffer = VK_NULL_HANDLE;
        buffer->memory = VK_NULL_HANDLE;
    }

    void Renderer::create_quad_mesh()
    {
        Vertex vertices[4] = {
            {vec3_create(-0.5f, -0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f)},
            {vec3_create(0.5f, -0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f)},
            {vec3_create(0.5f, 0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f)},
            {vec3_create(-0.5f, 0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f)}};

        uint32_t indices[6] = {0, 1, 2, 2, 3, 0};

        VkDeviceSize vertex_size = sizeof(vertices);
        create_buffer(vertex_size,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &quad_mesh_.vertex);

        void *data;
        vkMapMemory(device_, quad_mesh_.vertex.memory, 0, vertex_size, 0, &data);
        std::memcpy(data, vertices, vertex_size);
        vkUnmapMemory(device_, quad_mesh_.vertex.memory);

        quad_mesh_.index_count = 6;
        VkDeviceSize index_size = sizeof(indices);
        create_buffer(index_size,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &quad_mesh_.index);

        vkMapMemory(device_, quad_mesh_.index.memory, 0, index_size, 0, &data);
        std::memcpy(data, indices, index_size);
        vkUnmapMemory(device_, quad_mesh_.index.memory);
    }

}
