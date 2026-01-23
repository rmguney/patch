#include "renderer.h"
#include <cstring>

namespace patch
{

    void Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties, VulkanBuffer *buffer)
    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateFlags vma_flags = 0;
        if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        buffer->buffer = gpu_allocator_.create_buffer(buffer_info, VMA_MEMORY_USAGE_AUTO, vma_flags, &buffer->allocation);
    }

    void Renderer::destroy_buffer(VulkanBuffer *buffer)
    {
        if (!buffer)
            return;
        if (buffer->buffer || buffer->allocation)
            gpu_allocator_.destroy_buffer(buffer->buffer, buffer->allocation);
        buffer->buffer = VK_NULL_HANDLE;
        buffer->allocation = VK_NULL_HANDLE;
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

        void *data = gpu_allocator_.map(quad_mesh_.vertex.allocation);
        std::memcpy(data, vertices, vertex_size);
        gpu_allocator_.unmap(quad_mesh_.vertex.allocation);

        quad_mesh_.index_count = 6;
        VkDeviceSize index_size = sizeof(indices);
        create_buffer(index_size,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &quad_mesh_.index);

        data = gpu_allocator_.map(quad_mesh_.index.allocation);
        std::memcpy(data, indices, index_size);
        gpu_allocator_.unmap(quad_mesh_.index.allocation);
    }

    void Renderer::create_ui_buffers()
    {
        const uint32_t max_vertices = UI_MAX_QUADS * 4u;
        const uint32_t max_indices = UI_MAX_QUADS * 6u;

        ui_vertex_capacity_ = max_vertices;
        ui_index_capacity_ = max_indices;

        const VkDeviceSize vertex_size = sizeof(UIVertex) * (VkDeviceSize)max_vertices;
        create_buffer(vertex_size,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &ui_vertex_buffer_);

        ui_vertex_mapped_ = gpu_allocator_.map(ui_vertex_buffer_.allocation);

        const VkDeviceSize index_size = sizeof(uint32_t) * (VkDeviceSize)max_indices;
        create_buffer(index_size,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &ui_index_buffer_);

        ui_index_mapped_ = gpu_allocator_.map(ui_index_buffer_.allocation);

        ui_vertices_.reserve(max_vertices);
        ui_indices_.reserve(max_indices);
    }

}
