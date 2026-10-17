#include "ui_pass.h"
#include "ui_font.h"
#include "vk_pipeline.h"
#include "shaders_embedded.h"
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace patch
{

    /*
     * Glyph atlas: the printable ASCII characters, ATLAS_COLUMNS to a row, each in a cell of
     * CELL_W x CELL_H texels with its 5x7 glyph in the top-left corner (the empty texels keep
     * neighbours apart). The cell of DEL (127) is a solid block: untextured quads sample it.
     */
    static constexpr uint32_t GLYPH_FIRST = 32;
    static constexpr uint32_t GLYPH_SOLID = 127;
    static constexpr uint32_t GLYPH_COUNT = GLYPH_SOLID - GLYPH_FIRST + 1;
    static constexpr uint32_t GLYPH_W = 5;
    static constexpr uint32_t GLYPH_H = 7;
    static constexpr uint32_t GLYPH_ADVANCE = GLYPH_W + 1; /* a font pixel of space between characters */
    static constexpr uint32_t CELL_W = GLYPH_W + 1;
    static constexpr uint32_t CELL_H = GLYPH_H + 1;
    static constexpr uint32_t ATLAS_COLUMNS = 16;
    static constexpr uint32_t ATLAS_W = ATLAS_COLUMNS * CELL_W;
    static constexpr uint32_t ATLAS_H = (GLYPH_COUNT + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS * CELL_H;

    /* Top-left texel of a character's cell */
    static void glyph_cell(uint32_t c, uint32_t *x, uint32_t *y)
    {
        const uint32_t cell = c - GLYPH_FIRST;
        *x = cell % ATLAS_COLUMNS * CELL_W;
        *y = cell / ATLAS_COLUMNS * CELL_H;
    }

    bool UiPass::create(VulkanContext &ctx, VkFormat color_format)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        if (!create_set_layout(device, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT}},
                               &set_layout_) ||
            !create_pipeline_layout(device, {set_layout_}, 0, 0, &layout_) ||
            !create_descriptor_pool(device, 1, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, &pool_) ||
            !allocate_sets(device, pool_, set_layout_, 1, &set_) || !create_glyph_atlas())
            return false;

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(UIVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[3]{};
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = static_cast<uint32_t>(offsetof(UIVertex, x));
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = static_cast<uint32_t>(offsetof(UIVertex, u));
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[2].offset = static_cast<uint32_t>(offsetof(UIVertex, r));

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 3;
        vertex_input.pVertexAttributeDescriptions = attributes;

        GraphicsPipelineDesc desc{};
        desc.vertex = {shaders::k_shader_ui_vert_spv, shaders::k_shader_ui_vert_spv_size};
        desc.fragment = {shaders::k_shader_ui_frag_spv, shaders::k_shader_ui_frag_spv_size};
        desc.layout = layout_;
        desc.color_formats = &color_format;
        desc.color_count = 1;
        desc.alpha_blend = true;
        desc.vertex_input = &vertex_input;
        if (!create_graphics_pipeline(ctx, desc, &pipeline_))
            return false;

        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (!ctx.create_buffer(sizeof(UIVertex) * (VkDeviceSize)MAX_VERTICES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   host, &vertex_buffers_[i]) ||
                !ctx.create_buffer(sizeof(uint32_t) * (VkDeviceSize)MAX_INDICES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                   host, &index_buffers_[i]))
                return false;
        }

        vertices_.reserve(MAX_VERTICES);
        indices_.reserve(MAX_INDICES);
        return true;
    }

    bool UiPass::create_glyph_atlas()
    {
        ImageDesc desc;
        desc.format = VK_FORMAT_R8_UNORM;
        desc.extent = {ATLAS_W, ATLAS_H, 1};
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VulkanBuffer staging{};
        if (!ctx_->create_image(desc, &atlas_) ||
            !create_point_sampler(ctx_->device(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, &atlas_sampler_) ||
            !ctx_->create_buffer(ATLAS_W * ATLAS_H, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging))
            return false;

        uint8_t *texels = static_cast<uint8_t *>(staging.mapped);
        memset(texels, 0, ATLAS_W * ATLAS_H);
        for (uint32_t c = GLYPH_FIRST; c <= GLYPH_SOLID; c++)
        {
            uint8_t rows[GLYPH_H];
            if (c == GLYPH_SOLID)
                memset(rows, 0x1F, sizeof(rows));
            else
                font5x7_rows(rows, static_cast<char>(c));
            uint32_t cell_x = 0, cell_y = 0;
            glyph_cell(c, &cell_x, &cell_y);
            for (uint32_t ry = 0; ry < GLYPH_H; ry++)
            {
                for (uint32_t rx = 0; rx < GLYPH_W; rx++)
                {
                    if (rows[ry] & (1u << (GLYPH_W - 1 - rx)))
                        texels[(cell_y + ry) * ATLAS_W + cell_x + rx] = 0xFF;
                }
            }
        }

        VkCommandBuffer cmd = ctx_->begin_immediate_commands();
        if (cmd)
        {
            BarrierBatch barriers(cmd);
            barriers.discard(atlas_, image_use::copy_dst);
            barriers.record();

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = desc.extent;
            vkCmdCopyBufferToImage(cmd, staging.buffer, atlas_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barriers.transition(atlas_, image_use::sampled(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT));
            barriers.record();
        }
        const bool uploaded = cmd && ctx_->submit_immediate_commands(cmd);
        ctx_->destroy_buffer(&staging);
        if (!uploaded)
            return false;

        DescriptorWriter(ctx_->device()).sampled(set_, 0, atlas_sampler_, atlas_.view).update();
        return true;
    }

    void UiPass::destroy()
    {
        if (!ctx_)
            return;
        VkDevice device = ctx_->device();
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            ctx_->destroy_buffer(&vertex_buffers_[i]);
            ctx_->destroy_buffer(&index_buffers_[i]);
        }
        destroy_pipeline(device, &pipeline_);
        destroy_pipeline_layout(device, &layout_);
        destroy_descriptor_pool(device, &pool_);
        destroy_set_layout(device, &set_layout_);
        destroy_sampler(device, &atlas_sampler_);
        ctx_->destroy_image(&atlas_);
    }

    void UiPass::begin_frame()
    {
        vertex_used_ = 0;
        index_used_ = 0;
    }

    void UiPass::begin(VkCommandBuffer cmd)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
        vertices_.clear();
        indices_.clear();
    }

    void UiPass::end(VkCommandBuffer cmd, uint32_t slot)
    {
        if (indices_.empty())
            return;

        /* Several batches per frame (menus, debug overlay, loading screen) append to this
         * frame's buffers instead of overwriting data an earlier draw of the same frame uses. */
        uint32_t vertex_count = (uint32_t)vertices_.size();
        uint32_t index_count = (uint32_t)indices_.size();
        if (vertex_used_ + vertex_count > MAX_VERTICES || index_used_ + index_count > MAX_INDICES)
            return; /* frame budget exhausted (MAX_QUADS) */

        std::memcpy(static_cast<UIVertex *>(vertex_buffers_[slot].mapped) + vertex_used_,
                    vertices_.data(), vertex_count * sizeof(UIVertex));
        std::memcpy(static_cast<uint32_t *>(index_buffers_[slot].mapped) + index_used_,
                    indices_.data(), index_count * sizeof(uint32_t));

        VkDeviceSize offset = (VkDeviceSize)vertex_used_ * sizeof(UIVertex);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffers_[slot].buffer, &offset);
        vkCmdBindIndexBuffer(cmd, index_buffers_[slot].buffer, (VkDeviceSize)index_used_ * sizeof(uint32_t),
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);

        vertex_used_ += vertex_count;
        index_used_ += index_count;
    }

    void UiPass::add_quad_px(VkExtent2D extent, float x_px, float y_px, float w_px, float h_px, float u0, float v0,
                             float u1, float v1, Vec3 color, float alpha)
    {
        if (vertices_.size() + 4 > MAX_VERTICES || indices_.size() + 6 > MAX_INDICES)
        {
            return;
        }

        /* Pixels to clip space, whose y points down like the pixel rows */
        const float scale_x = 2.0f / ((extent.width > 0) ? (float)extent.width : 1.0f);
        const float scale_y = 2.0f / ((extent.height > 0) ? (float)extent.height : 1.0f);
        const float left = x_px * scale_x - 1.0f;
        const float right = (x_px + w_px) * scale_x - 1.0f;
        const float top = y_px * scale_y - 1.0f;
        const float bottom = (y_px + h_px) * scale_y - 1.0f;

        const uint32_t base_index = (uint32_t)vertices_.size();

        vertices_.push_back({left, top, u0, v0, color.x, color.y, color.z, alpha});
        vertices_.push_back({right, top, u1, v0, color.x, color.y, color.z, alpha});
        vertices_.push_back({right, bottom, u1, v1, color.x, color.y, color.z, alpha});
        vertices_.push_back({left, bottom, u0, v1, color.x, color.y, color.z, alpha});

        indices_.push_back(base_index + 0);
        indices_.push_back(base_index + 1);
        indices_.push_back(base_index + 2);
        indices_.push_back(base_index + 2);
        indices_.push_back(base_index + 3);
        indices_.push_back(base_index + 0);
    }

    void UiPass::quad_px(VkExtent2D extent, float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha)
    {
        /* Every corner on the centre of a texel of the solid block */
        uint32_t cell_x = 0, cell_y = 0;
        glyph_cell(GLYPH_SOLID, &cell_x, &cell_y);
        const float u = ((float)cell_x + 0.5f) / (float)ATLAS_W;
        const float v = ((float)cell_y + 0.5f) / (float)ATLAS_H;
        add_quad_px(extent, x_px, y_px, w_px, h_px, u, v, u, v, color, alpha);
    }

    void UiPass::text_px(VkExtent2D extent, float x_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text)
    {
        if (!text || text_h_px <= 0.0f)
        {
            return;
        }

        /* Font pixels on whole screen pixels: each texel of a glyph covers a unit x unit block */
        const float unit = ui_text_unit_px(text_h_px);
        const float y = std::floor(y_px + 0.5f);
        float x = std::floor(x_px + 0.5f);
        for (const char *p = text; *p; ++p, x += unit * (float)GLYPH_ADVANCE)
        {
            /* Spaces and characters without a glyph (control, non-ASCII) only advance */
            const uint32_t c = static_cast<unsigned char>(*p);
            if (c <= GLYPH_FIRST || c >= GLYPH_SOLID)
                continue;
            uint32_t cell_x = 0, cell_y = 0;
            glyph_cell(c, &cell_x, &cell_y);
            const float u0 = (float)cell_x / (float)ATLAS_W;
            const float v0 = (float)cell_y / (float)ATLAS_H;
            const float u1 = (float)(cell_x + GLYPH_W) / (float)ATLAS_W;
            const float v1 = (float)(cell_y + GLYPH_H) / (float)ATLAS_H;
            add_quad_px(extent, x, y, unit * (float)GLYPH_W, unit * (float)GLYPH_H, u0, v0, u1, v1, color, alpha);
        }
    }

}
