#ifndef PATCH_RENDER_GPU_TIMER_H
#define PATCH_RENDER_GPU_TIMER_H

#include <vulkan/vulkan.h>
#include "engine/render/vk_context.h"
#include <cstdint>

namespace patch
{

    struct GpuTimings
    {
        float total_gpu_ms;
        /* Per-pass breakdown (GpuTimestamp pairs) */
        float gbuffer_compute_ms;
        float shadow_compute_ms;
        float temporal_shadow_ms;
        float ao_compute_ms;
        float temporal_ao_ms;
        float render_pass_ms; /* G-buffer raster pass (or menu pass) to the end of the frame */
        float taa_resolve_ms;
        float spatial_denoise_ms;
    };

    /* Query indices within a frame slot's block: begin/end pairs */
    enum class GpuTimestamp : uint8_t
    {
        GBUFFER_BEGIN,
        GBUFFER_END,
        SHADOW_BEGIN,
        SHADOW_END,
        TEMPORAL_SHADOW_BEGIN,
        TEMPORAL_SHADOW_END,
        AO_BEGIN,
        AO_END,
        TEMPORAL_AO_BEGIN,
        TEMPORAL_AO_END,
        RENDER_BEGIN,
        RENDER_END,
        TAA_BEGIN,
        TAA_END,
        DENOISE_BEGIN,
        DENOISE_END,
        COUNT
    };

    /* Per-pass GPU profiling. Optional: without timestamp support nothing is recorded and the
     * overlay shows no GPU times. */
    class GpuTimer
    {
    public:
        void create(VulkanContext &ctx);
        void destroy();

        void reset(VkCommandBuffer cmd, uint32_t slot) const;
        void write(VkCommandBuffer cmd, uint32_t slot, GpuTimestamp timestamp, VkPipelineStageFlagBits stage) const;
        void mark_submitted(uint32_t slot) { submitted_[slot] = supported_; }

        /* Right after slot's fence was waited: every query its last submission wrote is complete */
        void collect(uint32_t slot);
        bool last(GpuTimings *out) const;

    private:
        static constexpr uint32_t QUERIES_PER_SLOT = static_cast<uint32_t>(GpuTimestamp::COUNT);

        bool timings_from(const uint64_t *query_data, GpuTimings *out) const;

        VkDevice device_ = VK_NULL_HANDLE;
        VkQueryPool pool_ = VK_NULL_HANDLE;
        float period_ns_ = 0.0f;
        uint64_t valid_mask_ = 0; /* the bits a timestamp holds */
        bool supported_ = false;
        bool submitted_[MAX_FRAMES_IN_FLIGHT] = {}; /* slot has results pending since its last submit */
        GpuTimings last_{};
        bool last_valid_ = false;
    };

}

#endif
