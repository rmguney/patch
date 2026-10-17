#ifndef PATCH_RENDER_FRAME_RESOURCES_H
#define PATCH_RENDER_FRAME_RESOURCES_H

#include <vulkan/vulkan.h>
#include "engine/render/vk_context.h"
#include "engine/render/gpu_timer.h"
#include <cstdint>

namespace patch
{

    /* What a pass needs to record into the current frame */
    struct FrameContext
    {
        VkCommandBuffer cmd;
        uint32_t slot;   /* frame in flight: selects per-frame descriptor sets and buffers */
        uint64_t number; /* frames begun so far, this one included (the first frame is 1) */
        const GpuTimer *timer;

        void timestamp(GpuTimestamp ts, VkPipelineStageFlagBits stage) const { timer->write(cmd, slot, ts, stage); }
    };

    /*
     * A temporal history (TAA, shadow and AO resolves) may be reprojected only if its pass wrote
     * it the frame just before: a frame its pass did not run in (turned off, skipped) leaves it
     * stale.
     */
    class TemporalHistory
    {
    public:
        bool valid(const FrameContext &frame) const { return written_ != 0 && written_ + 1 == frame.number; }
        void written(const FrameContext &frame) { written_ = frame.number; }
        void reset() { written_ = 0; }

    private:
        uint64_t written_ = 0; /* FrameContext::number of the last write, 0 = none */
    };

    /*
     * Per frame in flight: command buffer, fence, acquire semaphore, and the uniform buffers the
     * CPU rewrites every frame (scene lighting, last frame's view-projection). Slot i is reused
     * only after fences[i] was waited. The scene lighting set is bound by both the shadow
     * compute pass and the lighting fragment pass.
     */
    struct FrameResources
    {
        VkCommandBuffer cmd[MAX_FRAMES_IN_FLIGHT] = {};
        VkFence in_flight[MAX_FRAMES_IN_FLIGHT] = {};
        VkSemaphore image_available[MAX_FRAMES_IN_FLIGHT] = {};

        VulkanBuffer lighting_ubo[MAX_FRAMES_IN_FLIGHT] = {};
        VulkanBuffer temporal_ubo[MAX_FRAMES_IN_FLIGHT] = {};

        VkDescriptorSetLayout lighting_layout = VK_NULL_HANDLE;
        VkDescriptorPool lighting_pool = VK_NULL_HANDLE;
        VkDescriptorSet lighting_sets[MAX_FRAMES_IN_FLIGHT] = {};

        bool create(VulkanContext &ctx);
        void destroy(VulkanContext &ctx);
    };

}

#endif
