#ifndef PATCH_RENDER_VK_CONTEXT_H
#define PATCH_RENDER_VK_CONTEXT_H

#include <vulkan/vulkan.h>
#include "engine/render/gpu_memory.h"
#include "engine/render/vk_barriers.h"
#include <cstddef>
#include <cstdint>

namespace patch
{

    class Window;

    inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    /* Host-visible buffers are persistently mapped (mapped != nullptr) */
    struct VulkanBuffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void *mapped = nullptr;
    };

    /* A device-local image with one view over all of it, tracked for barriers */
    struct GpuImage : TrackedImage
    {
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct ImageDesc
    {
        VkImageType type = VK_IMAGE_TYPE_2D;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent3D extent{1, 1, 1};
        uint32_t mip_levels = 1;
        VkImageUsageFlags usage = 0;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    };

    /*
     * Instance, debug messenger, surface, physical and logical device, queues, memory allocator,
     * pipeline cache and the command pool every command buffer comes from. Also the resource
     * helpers the subsystems share. destroy() is safe on a partially created context.
     */
    class VulkanContext
    {
    public:
        /* On failure, *error names the step that failed */
        bool create(Window &window, const char **error);
        void destroy();

        VkPhysicalDevice physical_device() const { return physical_device_; }
        VkDevice device() const { return device_; }
        VkSurfaceKHR surface() const { return surface_; }
        VkQueue graphics_queue() const { return graphics_queue_; }
        VkQueue present_queue() const { return present_queue_; }
        uint32_t graphics_family() const { return graphics_family_; }
        uint32_t present_family() const { return present_family_; }
        VkPipelineCache pipeline_cache() const { return pipeline_cache_; }

        const char *gpu_name() const { return gpu_name_; }
        /* VK_EXT_swapchain_maintenance1 is enabled: presents can signal fences */
        bool present_fences_supported() const { return swapchain_maintenance1_; }

        /* Timestamps on the graphics queue: their valid low bits (0 = not supported) and tick length */
        uint32_t timestamp_valid_bits() const { return timestamp_valid_bits_; }
        float timestamp_period_ns() const { return timestamp_period_ns_; }

        /* HOST_VISIBLE buffers must also request HOST_COHERENT; they come back mapped */
        /* own_memory: a memory allocation of its own, which goes back to the system when the
         * buffer is destroyed instead of staying in a shared block for reuse */
        bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                           VulkanBuffer *buffer, bool own_memory = false);
        void destroy_buffer(VulkanBuffer *buffer);

        /* Image (optimal tiling, one sample, UNDEFINED) and a view over all mips */
        bool create_image(const ImageDesc &desc, GpuImage *image);
        void destroy_image(GpuImage *image);

        bool allocate_command_buffers(uint32_t count, VkCommandBuffer *out);

        /* Between frames only (init, scene loads, resizes): blocks until the queue is idle.
         * Never used inside a frame. False when the commands could not be executed (device lost). */
        VkCommandBuffer begin_immediate_commands();
        bool submit_immediate_commands(VkCommandBuffer cmd);

    private:
        bool create_instance();
        bool create_debug_messenger();
        void destroy_debug_messenger();
        void print_validation_summary() const;
        bool select_physical_device(const char **error);
        bool find_queue_families();
        bool create_logical_device(const char **error);
        void create_pipeline_cache();
        void save_pipeline_cache();

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
        bool validation_enabled_ = false;
        bool sync_validation_enabled_ = false;
        bool surface_maintenance1_ = false;   /* instance: VK_EXT_surface_maintenance1 */
        bool swapchain_maintenance1_ = false; /* device: VK_EXT_swapchain_maintenance1 */
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        GpuAllocator allocator_;
        VkQueue graphics_queue_ = VK_NULL_HANDLE;
        VkQueue present_queue_ = VK_NULL_HANDLE;
        uint32_t graphics_family_ = UINT32_MAX;
        uint32_t present_family_ = UINT32_MAX;
        VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
        VkCommandPool command_pool_ = VK_NULL_HANDLE;

        char gpu_name_[256] = {};
        uint32_t timestamp_valid_bits_ = 0;
        float timestamp_period_ns_ = 0.0f;
    };

    const char *vk_result_name(VkResult result);

    inline void cmd_set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent)
    {
        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

}

#endif
