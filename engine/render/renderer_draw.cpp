#include "renderer.h"
#include "renderer_internal.h"
#include "ui_font.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    void Renderer::begin_ui()
    {
        bind_pipeline(ui_pipeline_);

        VkBuffer vertex_buffers[] = {quad_mesh_.vertex.buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], quad_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    void Renderer::end_ui()
    {
        /* No state restoration needed - pure deferred pipeline */
    }

    void Renderer::draw_ui_quad(float cx, float cy, float w, float h, Vec3 color, float alpha)
    {
        Mat4 view = mat4_identity();
        Mat4 proj = mat4_identity();

        PushConstants pc;
        pc.model = mat4_translate_scale_clip(cx, -cy, w, h);
        pc.view = view;
        pc.projection = proj;
        pc.color_alpha = {color.x, color.y, color.z, alpha};
        pc.params = {0.0f, 0.0f, 0.0f, 0.0f};
        vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(command_buffers_[current_frame_], quad_mesh_.index_count, 1, 0, 0, 0);
    }

    void Renderer::draw_ui_text(float x_left, float y_top, float pixel, Vec3 color, float alpha, const char *text)
    {
        Mat4 view = mat4_identity();
        Mat4 proj = mat4_identity();

        float x = x_left;
        const float w = (swapchain_extent_.width > 0) ? (float)swapchain_extent_.width : 1.0f;
        const float h = (swapchain_extent_.height > 0) ? (float)swapchain_extent_.height : 1.0f;
        const float pixel_x = pixel * (h / w);
        const float pixel_y = pixel;
        for (const char *p = text; *p; ++p)
        {
            uint8_t rows[7];
            font5x7_rows(rows, *p);
            for (int ry = 0; ry < 7; ry++)
            {
                uint8_t bits = rows[ry];
                for (int rx = 0; rx < 5; rx++)
                {
                    if (bits & (1u << (4 - rx)))
                    {
                        float cx = x + (float)rx * pixel_x + pixel_x * 0.5f;
                        float cy = y_top - (float)ry * pixel_y - pixel_y * 0.5f;

                        PushConstants pc;
                        pc.model = mat4_translate_scale_clip(cx, -cy, pixel_x, pixel_y);
                        pc.view = view;
                        pc.projection = proj;
                        pc.color_alpha = {color.x, color.y, color.z, alpha};
                        pc.params = {0.0f, 0.0f, 0.0f, 0.0f};
                        vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(PushConstants), &pc);
                        vkCmdDrawIndexed(command_buffers_[current_frame_], quad_mesh_.index_count, 1, 0, 0, 0);
                    }
                }
            }
            x += pixel_x * 6.0f;
        }
    }

    void Renderer::draw_ui_quad_px(float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha)
    {
        const float w = (swapchain_extent_.width > 0) ? (float)swapchain_extent_.width : 1.0f;
        const float h = (swapchain_extent_.height > 0) ? (float)swapchain_extent_.height : 1.0f;

        const float cx_ndc = ((x_px + w_px * 0.5f) / w) * 2.0f - 1.0f;
        const float cy_ndc = 1.0f - ((y_px + h_px * 0.5f) / h) * 2.0f;
        const float ww_ndc = (w_px / w) * 2.0f;
        const float hh_ndc = (h_px / h) * 2.0f;

        draw_ui_quad(cx_ndc, cy_ndc, ww_ndc, hh_ndc, color, alpha);
    }

    void Renderer::draw_ui_text_px(float x_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text)
    {
        if (!text || text_h_px <= 0.0f)
        {
            return;
        }

        const float w = (swapchain_extent_.width > 0) ? (float)swapchain_extent_.width : 1.0f;
        const float h = (swapchain_extent_.height > 0) ? (float)swapchain_extent_.height : 1.0f;

        const float x_left_ndc = (x_px / w) * 2.0f - 1.0f;
        const float y_top_ndc = 1.0f - (y_px / h) * 2.0f;
        const float unit_px = text_h_px / 7.0f;
        const float pixel_y_ndc = (unit_px / h) * 2.0f;

        draw_ui_text(x_left_ndc, y_top_ndc, pixel_y_ndc, color, alpha, text);
    }

}
