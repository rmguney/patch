#include "frame_resources.h"
#include "vk_pipeline.h"
#include "voxel_push_constants.h"

namespace patch
{

    bool FrameResources::create(VulkanContext &ctx)
    {
        VkDevice device = ctx.device();
        if (!ctx.allocate_command_buffers(MAX_FRAMES_IN_FLIGHT, cmd))
            return false;

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fence_info, nullptr, &in_flight[i]) != VK_SUCCESS ||
                !ctx.create_buffer(sizeof(SceneLightingUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, &lighting_ubo[i]) ||
                !ctx.create_buffer(sizeof(VoxelTemporalUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, &temporal_ubo[i]))
                return false;
        }

        if (!create_set_layout(device, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT}},
                               &lighting_layout) ||
            !create_descriptor_pool(device, MAX_FRAMES_IN_FLIGHT,
                                    {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT}}, &lighting_pool) ||
            !allocate_sets(device, lighting_pool, lighting_layout, MAX_FRAMES_IN_FLIGHT, lighting_sets))
            return false;

        DescriptorWriter writer(device);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            writer.buffer(lighting_sets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, lighting_ubo[i].buffer, sizeof(SceneLightingUBO));
        writer.update();
        return true;
    }

    void FrameResources::destroy(VulkanContext &ctx)
    {
        VkDevice device = ctx.device();
        destroy_descriptor_pool(device, &lighting_pool);
        destroy_set_layout(device, &lighting_layout);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            lighting_sets[i] = VK_NULL_HANDLE;
            ctx.destroy_buffer(&lighting_ubo[i]);
            ctx.destroy_buffer(&temporal_ubo[i]);
            if (image_available[i])
                vkDestroySemaphore(device, image_available[i], nullptr);
            if (in_flight[i])
                vkDestroyFence(device, in_flight[i], nullptr);
            image_available[i] = VK_NULL_HANDLE;
            in_flight[i] = VK_NULL_HANDLE;
            cmd[i] = VK_NULL_HANDLE; /* freed with the context's command pool */
        }
    }

}
