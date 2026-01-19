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
        ui_vertices_.clear();
        ui_indices_.clear();
        /* Reserve capacity to avoid reallocation during text rendering.
         * ~2000 characters * 14 lit pixels avg = 28000 quads = 112000 vertices, 168000 indices */
        ui_vertices_.reserve(32768);
        ui_indices_.reserve(49152);
    }

    void Renderer::end_ui()
    {
        if (ui_vertices_.empty() || ui_indices_.empty())
        {
            return;
        }

        const size_t vertex_bytes = ui_vertices_.size() * sizeof(UIVertex);
        const size_t index_bytes = ui_indices_.size() * sizeof(uint32_t);
        std::memcpy(ui_vertex_mapped_, ui_vertices_.data(), vertex_bytes);
        std::memcpy(ui_index_mapped_, ui_indices_.data(), index_bytes);

        VkBuffer vertex_buffers[] = {ui_vertex_buffer_.buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], ui_index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(command_buffers_[current_frame_], (uint32_t)ui_indices_.size(), 1, 0, 0, 0);
    }

    void Renderer::add_ui_quad_ndc(float cx, float cy, float w, float h, Vec3 color, float alpha)
    {
        if (ui_vertices_.size() + 4 > ui_vertex_capacity_ || ui_indices_.size() + 6 > ui_index_capacity_)
        {
            return;
        }

        const float half_w = w * 0.5f;
        const float half_h = h * 0.5f;
        const float left = cx - half_w;
        const float right = cx + half_w;
        const float bottom = cy - half_h;
        const float top = cy + half_h;

        const uint32_t base_index = (uint32_t)ui_vertices_.size();

        ui_vertices_.push_back({left, bottom, color.x, color.y, color.z, alpha});
        ui_vertices_.push_back({right, bottom, color.x, color.y, color.z, alpha});
        ui_vertices_.push_back({right, top, color.x, color.y, color.z, alpha});
        ui_vertices_.push_back({left, top, color.x, color.y, color.z, alpha});

        ui_indices_.push_back(base_index + 0);
        ui_indices_.push_back(base_index + 1);
        ui_indices_.push_back(base_index + 2);
        ui_indices_.push_back(base_index + 2);
        ui_indices_.push_back(base_index + 3);
        ui_indices_.push_back(base_index + 0);
    }

    void Renderer::draw_ui_quad(float cx, float cy, float w, float h, Vec3 color, float alpha)
    {
        add_ui_quad_ndc(cx, -cy, w, h, color, alpha);
    }

    void Renderer::draw_ui_text(float x_left, float y_top, float pixel, Vec3 color, float alpha, const char *text)
    {
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
                        add_ui_quad_ndc(cx, -cy, pixel_x, pixel_y, color, alpha);
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
