#include "gpu_timer.h"
#include <cstdio>

namespace patch
{

    void GpuTimer::create(VulkanContext &ctx)
    {
        device_ = ctx.device();
        period_ns_ = ctx.timestamp_period_ns();
        const uint32_t valid_bits = ctx.timestamp_valid_bits();
        valid_mask_ = valid_bits >= 64 ? ~uint64_t{0} : (uint64_t{1} << valid_bits) - 1;
        supported_ = valid_bits > 0;
        if (!supported_)
        {
            printf("  GPU timestamps not supported\n");
            return;
        }

        VkQueryPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        pool_info.queryCount = QUERIES_PER_SLOT * MAX_FRAMES_IN_FLIGHT;

        if (vkCreateQueryPool(device_, &pool_info, nullptr, &pool_) != VK_SUCCESS)
        {
            printf("  Failed to create timestamp query pool\n");
            supported_ = false;
        }
    }

    void GpuTimer::destroy()
    {
        if (pool_ != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }

    void GpuTimer::reset(VkCommandBuffer cmd, uint32_t slot) const
    {
        if (supported_)
            vkCmdResetQueryPool(cmd, pool_, slot * QUERIES_PER_SLOT, QUERIES_PER_SLOT);
    }

    void GpuTimer::write(VkCommandBuffer cmd, uint32_t slot, GpuTimestamp timestamp, VkPipelineStageFlagBits stage) const
    {
        if (supported_)
            vkCmdWriteTimestamp(cmd, stage, pool_, slot * QUERIES_PER_SLOT + static_cast<uint32_t>(timestamp));
    }

    /* Passes that did not run this frame (quality 0, menu) leave their queries unavailable, so
     * each pair is checked individually through the availability word */
    void GpuTimer::collect(uint32_t slot)
    {
        if (!supported_ || !submitted_[slot])
            return;
        submitted_[slot] = false;

        uint64_t data[QUERIES_PER_SLOT * 2] = {};
        VkResult result = vkGetQueryPoolResults(
            device_, pool_, slot * QUERIES_PER_SLOT, QUERIES_PER_SLOT,
            sizeof(data), data, sizeof(uint64_t) * 2,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        /* VK_NOT_READY just means some queries were not written this frame */
        if (result != VK_SUCCESS && result != VK_NOT_READY)
            return;

        GpuTimings timings{};
        if (timings_from(data, &timings))
        {
            last_ = timings;
            last_valid_ = true;
        }
    }

    bool GpuTimer::timings_from(const uint64_t *data, GpuTimings *out) const
    {
        bool any_pair = false;
        for (uint32_t p = 0; p < QUERIES_PER_SLOT / 2; p++)
        {
            if (data[(2 * p) * 2 + 1] != 0 && data[(2 * p + 1) * 2 + 1] != 0)
                any_pair = true;
        }
        if (!any_pair)
            return false;

        const float ns_to_ms = period_ns_ / 1000000.0f;

        /* Ticks from one timestamp to another, modulo the valid bits and read as signed: the
         * counter may wrap, and a frame spans far less than half its range */
        auto ticks = [&](uint64_t from, uint64_t to) -> int64_t
        {
            const uint64_t d = (to - from) & valid_mask_;
            return d > (valid_mask_ >> 1) ? static_cast<int64_t>(d - valid_mask_) - 1 : static_cast<int64_t>(d);
        };

        /* Duration of a begin/end pair when both were written */
        auto pass_ms = [&](GpuTimestamp begin, GpuTimestamp end) -> float
        {
            const uint32_t b = static_cast<uint32_t>(begin);
            const uint32_t e = static_cast<uint32_t>(end);
            if (data[b * 2 + 1] == 0 || data[e * 2 + 1] == 0)
                return 0.0f;
            return (float)ticks(data[b * 2], data[e * 2]) * ns_to_ms;
        };

        out->gbuffer_compute_ms = pass_ms(GpuTimestamp::GBUFFER_BEGIN, GpuTimestamp::GBUFFER_END);
        out->shadow_compute_ms = pass_ms(GpuTimestamp::SHADOW_BEGIN, GpuTimestamp::SHADOW_END);
        out->temporal_shadow_ms = pass_ms(GpuTimestamp::TEMPORAL_SHADOW_BEGIN, GpuTimestamp::TEMPORAL_SHADOW_END);
        out->ao_compute_ms = pass_ms(GpuTimestamp::AO_BEGIN, GpuTimestamp::AO_END);
        out->temporal_ao_ms = pass_ms(GpuTimestamp::TEMPORAL_AO_BEGIN, GpuTimestamp::TEMPORAL_AO_END);
        out->render_pass_ms = pass_ms(GpuTimestamp::RENDER_BEGIN, GpuTimestamp::RENDER_END);
        out->taa_resolve_ms = pass_ms(GpuTimestamp::TAA_BEGIN, GpuTimestamp::TAA_END);
        out->spatial_denoise_ms = pass_ms(GpuTimestamp::DENOISE_BEGIN, GpuTimestamp::DENOISE_END);

        /* Total GPU: earliest to latest available timestamp, relative to any one of them */
        bool have_reference = false;
        uint64_t reference = 0;
        int64_t first = 0;
        int64_t last = 0;
        for (uint32_t i = 0; i < QUERIES_PER_SLOT; i++)
        {
            if (data[i * 2 + 1] == 0)
                continue;
            if (!have_reference)
            {
                reference = data[i * 2];
                have_reference = true;
            }
            const int64_t t = ticks(reference, data[i * 2]);
            first = t < first ? t : first;
            last = t > last ? t : last;
        }
        out->total_gpu_ms = (float)(last - first) * ns_to_ms;
        return true;
    }

    bool GpuTimer::last(GpuTimings *out) const
    {
        if (!out || !last_valid_)
            return false;
        *out = last_;
        return true;
    }

}
