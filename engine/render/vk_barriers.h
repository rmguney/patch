#ifndef PATCH_RENDER_VK_BARRIERS_H
#define PATCH_RENDER_VK_BARRIERS_H

#include <vulkan/vulkan.h>
#include <cstdint>

namespace patch
{

    /*
     * Synchronization2 barriers. A tracked image remembers its layout and the scope (stages and
     * accesses) of its last use, so a transition names only the next use: the old layout and the
     * source scope of the barrier follow from what was recorded before. All barriers queued in a
     * BarrierBatch go out in one vkCmdPipelineBarrier2.
     *
     * The state follows recording order. Every command buffer goes to the one graphics queue in
     * the order its commands were recorded, so it is also the order the GPU sees them in.
     */

    /* One use of a resource: the stages that touch it and the accesses they make */
    struct GpuAccess
    {
        VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
    };

    /* Image layout plus the scope of its last use. Consecutive reads in one layout widen the
     * scope, so the next writer waits for every one of them. */
    struct ImageState
    {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
    };

    struct TrackedImage
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageSubresourceRange range{};
        ImageState state{};
    };

    namespace image_use
    {
        constexpr ImageState sampled(VkPipelineStageFlags2 stages)
        {
            return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        }
        inline constexpr ImageState compute_write{VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
        /* Loaded or blended attachment */
        inline constexpr ImageState color_attachment{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        /* Cleared, then overwritten without blending */
        inline constexpr ImageState color_attachment_write{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                           VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        inline constexpr ImageState depth_attachment{VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        inline constexpr ImageState copy_dst{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                                             VK_ACCESS_2_TRANSFER_WRITE_BIT};
        inline constexpr ImageState clear_dst{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                              VK_ACCESS_2_TRANSFER_WRITE_BIT};
        /* The queue submit's semaphore signal orders presentation after the frame */
        inline constexpr ImageState present{VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
    }

    /*
     * The barrier that takes an image from `state` to `to` (stages, accesses and layouts; image
     * and subresource range are left to the caller), and the image's state after it. Returns
     * false when no barrier is needed: a further read in the same layout by stages the last
     * barrier already made the contents visible to. discard: old layout UNDEFINED.
     */
    bool derive_image_barrier(ImageState &state, const ImageState &to, bool discard, VkImageMemoryBarrier2 *barrier);

    class BarrierBatch
    {
    public:
        explicit BarrierBatch(VkCommandBuffer cmd) : cmd_(cmd) {}

        /* Moves the image to `to`. A run of reads that the previous barrier already made the
         * image visible to needs no barrier and queues nothing. */
        void transition(TrackedImage &image, const ImageState &to);

        /* Same, but the current contents are not needed (old layout UNDEFINED). The source scope
         * still covers the last use: a discard is a write that must not overtake it. */
        void discard(TrackedImage &image, const ImageState &to);

        void buffer(VkBuffer buffer, const GpuAccess &from, const GpuAccess &to);

        /* Records everything queued since the last record() in one vkCmdPipelineBarrier2 */
        void record();

    private:
        void queue_image(TrackedImage &image, const ImageState &to, bool discard);

        static constexpr uint32_t MAX_IMAGES = 16;
        static constexpr uint32_t MAX_BUFFERS = 4;

        VkCommandBuffer cmd_;
        VkImageMemoryBarrier2 images_[MAX_IMAGES];
        VkBufferMemoryBarrier2 buffers_[MAX_BUFFERS];
        uint32_t image_count_ = 0;
        uint32_t buffer_count_ = 0;
    };

}

#endif
