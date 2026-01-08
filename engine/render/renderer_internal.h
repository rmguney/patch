#ifndef PATCH_RENDER_RENDERER_INTERNAL_H
#define PATCH_RENDER_RENDERER_INTERNAL_H

#include "renderer.h"
#include <vulkan/vulkan.h>

namespace patch
{

inline Mat4 mat4_translate_scale_clip(float center_x, float center_y, float sx, float sy)
{
    Mat4 t = mat4_translation(vec3_create(center_x, center_y, 0.0f));
    Mat4 s = mat4_scaling(vec3_create(sx, sy, 1.0f));
    return mat4_multiply(t, s);
}

inline void cmd_set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent)
{
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

}

#endif
