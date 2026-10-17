#include "vk_barriers.h"

namespace patch
{

    /* Accesses that have to be made available before a later access may touch the memory */
    static constexpr VkAccessFlags2 WRITE_ACCESS =
        VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT |
        VK_ACCESS_2_MEMORY_WRITE_BIT;

    bool derive_image_barrier(ImageState &state, const ImageState &to, bool discard, VkImageMemoryBarrier2 *barrier)
    {
        const bool same_layout = !discard && state.layout == to.layout;
        const bool reads_only = ((state.access | to.access) & WRITE_ACCESS) == 0;
        if (same_layout && reads_only && (to.stages & ~state.stages) == 0 && (to.access & ~state.access) == 0)
            return false;

        /* Reads need no availability operation: only the writes in the source scope do */
        *barrier = {};
        barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier->srcStageMask = state.stages;
        barrier->srcAccessMask = state.access & WRITE_ACCESS;
        barrier->dstStageMask = to.stages;
        barrier->dstAccessMask = to.access;
        barrier->oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : state.layout;
        barrier->newLayout = to.layout;
        barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        if (same_layout && reads_only)
        {
            /* A further reader of the same contents: the barrier chains after the earlier
             * readers, which the last write was made visible to */
            state.stages |= to.stages;
            state.access |= to.access;
        }
        else
        {
            state = to;
        }
        return true;
    }

    void BarrierBatch::transition(TrackedImage &image, const ImageState &to)
    {
        queue_image(image, to, false);
    }

    void BarrierBatch::discard(TrackedImage &image, const ImageState &to)
    {
        queue_image(image, to, true);
    }

    void BarrierBatch::queue_image(TrackedImage &image, const ImageState &to, bool discard)
    {
        VkImageMemoryBarrier2 barrier;
        if (!derive_image_barrier(image.state, to, discard, &barrier))
            return;

        if (image_count_ == MAX_IMAGES)
            record();
        barrier.image = image.image;
        barrier.subresourceRange = image.range;
        images_[image_count_++] = barrier;
    }

    void BarrierBatch::buffer(VkBuffer buffer, const GpuAccess &from, const GpuAccess &to)
    {
        if (buffer_count_ == MAX_BUFFERS)
            record();

        VkBufferMemoryBarrier2 &b = buffers_[buffer_count_++];
        b = {};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        b.srcStageMask = from.stages;
        b.srcAccessMask = from.access & WRITE_ACCESS;
        b.dstStageMask = to.stages;
        b.dstAccessMask = to.access;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buffer;
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;
    }

    void BarrierBatch::record()
    {
        if (image_count_ == 0 && buffer_count_ == 0)
            return;

        VkDependencyInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.bufferMemoryBarrierCount = buffer_count_;
        info.pBufferMemoryBarriers = buffers_;
        info.imageMemoryBarrierCount = image_count_;
        info.pImageMemoryBarriers = images_;
        vkCmdPipelineBarrier2(cmd_, &info);

        image_count_ = 0;
        buffer_count_ = 0;
    }

}
