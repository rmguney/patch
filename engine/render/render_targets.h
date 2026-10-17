#ifndef PATCH_RENDER_RENDER_TARGETS_H
#define PATCH_RENDER_RENDER_TARGETS_H

#include <vulkan/vulkan.h>
#include "engine/render/vk_context.h"
#include <cstdint>

namespace patch
{

    /*
     * Every image sized to the swapchain, created and destroyed as one group. The passes write
     * the descriptors that reference them in their bind_targets(); after a resolution change
     * the targets and those descriptor writes are rebuilt together, pipelines stay.
     */
    struct RenderTargets
    {
        /* No world position: the shading passes rebuild it from the linear depth and the pixel's
         * primary ray (shaders/include/gbuffer_sample.glsl) */
        static constexpr uint32_t GBUFFER_ALBEDO = 0;
        static constexpr uint32_t GBUFFER_NORMAL = 1;
        static constexpr uint32_t GBUFFER_MATERIAL = 2;
        static constexpr uint32_t GBUFFER_LINEAR_DEPTH = 3;
        static constexpr uint32_t GBUFFER_COUNT = 4;

        static constexpr VkFormat MOTION_FORMAT = VK_FORMAT_R16G16_SFLOAT;
        static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
        /* Accumulated over many frames: 8 bits would stall a slow change short of its value */
        static constexpr VkFormat TAA_HISTORY_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

        /* Colour attachments of the G-buffer raster pass, in attachment order: the G-buffer,
         * then motion vectors */
        static constexpr uint32_t RASTER_COLOR_COUNT = GBUFFER_COUNT + 1;
        static constexpr VkFormat RASTER_COLOR_FORMATS[RASTER_COLOR_COUNT] = {
            VK_FORMAT_R8G8B8A8_UNORM,           /* albedo */
            VK_FORMAT_A2B10G10R10_UNORM_PACK32, /* normal */
            VK_FORMAT_R32G32_UINT,              /* material (gbuffer_sample.glsl) */
            VK_FORMAT_R32_SFLOAT,               /* linear depth */
            MOTION_FORMAT};

        VkExtent2D extent{};
        VkSampler sampler = VK_NULL_HANDLE; /* nearest, clamped: how most passes read these */

        GpuImage gbuffer[GBUFFER_COUNT];
        GpuImage motion;
        GpuImage depth;
        GpuImage lit_color; /* lighting output before denoise / TAA (swapchain format) */
        GpuImage denoised;  /* the denoise's output when TAA follows it (swapchain format) */
        GpuImage shadow;    /* raw shadow trace */
        GpuImage shadow_history[2];
        GpuImage ao;
        GpuImage ao_history[2]; /* resolved AO and the linear depth it was resolved at */
        GpuImage taa_history[2]; /* resolved frames, TAA_HISTORY_FORMAT */

        bool create(VulkanContext &ctx, VkExtent2D size, VkFormat color_format);
        void destroy(VulkanContext &ctx);

        /* Attachment i of the G-buffer raster pass (RASTER_COLOR_FORMATS order) */
        GpuImage &raster_color(uint32_t i) { return i < GBUFFER_COUNT ? gbuffer[i] : motion; }
    };

}

#endif
