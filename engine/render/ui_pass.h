#ifndef PATCH_RENDER_UI_PASS_H
#define PATCH_RENDER_UI_PASS_H

#include <vulkan/vulkan.h>
#include "engine/core/math.h"
#include "engine/render/vk_context.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace patch
{

    struct UIVertex
    {
        float x;
        float y;
        float u;
        float v;
        float r;
        float g;
        float b;
        float a;
    };
    static_assert(sizeof(UIVertex) == 32, "UIVertex must be 32 bytes");

    /* Screen pixels per font pixel of text text_h_px tall: whole pixels keep the 5x7 font crisp */
    inline float ui_text_unit_px(float text_h_px)
    {
        const float unit = std::floor(text_h_px / 7.0f + 0.5f);
        return unit > 1.0f ? unit : 1.0f;
    }

    /*
     * Alpha-blended 2D quads (menus, overlay, loading screen) drawn into the swapchain image's
     * rendering scope: solid rectangles, and text as one quad per character sampling a glyph
     * atlas of the 5x7 font. A batch is collected between begin() and end(); several batches per
     * frame append to the frame slot's vertex / index buffers at a running offset.
     */
    class UiPass
    {
    public:
        bool create(VulkanContext &ctx, VkFormat color_format);
        void destroy();

        /* The slot's buffers are free again (its fence was waited) */
        void begin_frame();

        void begin(VkCommandBuffer cmd);
        void end(VkCommandBuffer cmd, uint32_t slot);

        /* Pixel-space helpers (top-left origin, sizes in pixels). Text snaps to whole pixels
         * with ui_text_unit_px() pixels per font pixel. */
        void quad_px(VkExtent2D extent, float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha);
        void text_px(VkExtent2D extent, float x_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text);

    private:
        static constexpr uint32_t MAX_QUADS = 20000;
        static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;
        static constexpr uint32_t MAX_INDICES = MAX_QUADS * 6;

        /* uv0 / uv1: atlas coordinates of the top-left / bottom-right corner */
        void add_quad_px(VkExtent2D extent, float x_px, float y_px, float w_px, float h_px, float u0, float v0, float u1,
                         float v1, Vec3 color, float alpha);
        bool create_glyph_atlas();

        VulkanContext *ctx_ = nullptr;
        VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet set_ = VK_NULL_HANDLE;
        GpuImage atlas_;
        VkSampler atlas_sampler_ = VK_NULL_HANDLE;

        /* The CPU writes frame N's UI while the GPU may still draw frame N-1's from the other pair */
        VulkanBuffer vertex_buffers_[MAX_FRAMES_IN_FLIGHT] = {};
        VulkanBuffer index_buffers_[MAX_FRAMES_IN_FLIGHT] = {};
        uint32_t vertex_used_ = 0; /* vertices already written this frame */
        uint32_t index_used_ = 0;  /* indices already written this frame */
        std::vector<UIVertex> vertices_;
        std::vector<uint32_t> indices_;
    };

}

#endif
