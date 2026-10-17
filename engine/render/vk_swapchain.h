#ifndef PATCH_RENDER_VK_SWAPCHAIN_H
#define PATCH_RENDER_VK_SWAPCHAIN_H

#include <vulkan/vulkan.h>
#include "engine/render/vk_barriers.h"
#include <cstdint>
#include <vector>

namespace patch
{

    class VulkanContext;
    class Window;

    /*
     * Swapchain, its image views and per-image present semaphores, and the rebuild state
     * machine: an out-of-date or suboptimal swapchain, or one whose surface changed size, is
     * rebuilt before the next acquire; a WM_SIZE is checked against the surface extent first.
     * Calls that return false either skip the frame (minimized, rebuild pending) or leave the
     * swapchain failed(), which needs a shutdown (surface or device lost, failed rebuild).
     */
    class Swapchain
    {
    public:
        enum class Status : uint8_t
        {
            Ready,   /* acquire as usual */
            Rebuild, /* call rebuild() first */
            Skip     /* no frame now: surface 0x0 (minimized), or failed() */
        };

        bool create(VulkanContext &ctx, Window &window);
        void destroy();

        void request_resize_check() { resize_pending_ = true; }

        /* Settles a pending resize check: a surface whose size differs needs a rebuild */
        Status prepare();

        /* The device must be idle. Rebuilds at the surface's current size, which may differ */
        bool rebuild();

        /* image_available is signaled only when this returns true */
        bool acquire(VkSemaphore image_available, uint32_t *image_index);

        /* Waits for render_finished(image_index), which the frame's submit signals */
        void present(uint32_t image_index);

        bool failed() const { return failed_; }

        VkFormat format() const { return format_; }
        VkExtent2D extent() const { return extent_; }
        uint32_t image_count() const { return static_cast<uint32_t>(images_.size()); }
        VkImageView view(uint32_t index) const { return views_[index]; }
        VkSemaphore render_finished(uint32_t index) const { return render_finished_[index]; }

        /* Acquired images start UNDEFINED at the acquire semaphore's wait stage */
        TrackedImage &image(uint32_t index) { return images_[index]; }

    private:
        /* A present's fence (VK_EXT_swapchain_maintenance1): signaled once the presentation
         * engine is done with the present's semaphore */
        struct PresentFence
        {
            VkFence fence = VK_NULL_HANDLE;
            bool pending = false; /* handed to a present that was not waited for since */
        };

        /*
         * The present semaphores of a swapchain that was rebuilt, and that swapchain. A device
         * idle does not cover the presentation engine's semaphore waits, so they live until its
         * presents are known to be done: by their present fences, or without those, once later
         * swapchains presented enough frames to have moved every older present through.
         */
        struct Retired
        {
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            std::vector<VkSemaphore> semaphores;
            std::vector<VkFence> fences; /* of the presents still pending */
            uint32_t presents_left = 0;  /* without present fences */
        };

        bool create_swapchain(VkSwapchainKHR old_swapchain);
        bool create_present_sync();
        /* Destroys the image views and retires `swapchain` (if any) with its present semaphores */
        void retire_swapchain_objects(VkSwapchainKHR swapchain);
        /* Destroys the retired objects whose presents are done; with `all`, every one of them
         * (shutdown: after a bounded wait for their present fences) */
        void release_retired(bool all);
        bool query_surface_extent(VkExtent2D *out_extent, VkSurfaceCapabilitiesKHR *out_caps = nullptr);

        VulkanContext *ctx_ = nullptr;
        Window *window_ = nullptr;

        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        VkExtent2D extent_{};
        std::vector<TrackedImage> images_;
        std::vector<VkImageView> views_;
        /* Per image, not per frame slot: the presentation engine may hold one past its frame's fence */
        std::vector<VkSemaphore> render_finished_;
        std::vector<PresentFence> present_fences_; /* per image; empty without present fences */
        std::vector<Retired> retired_;

        bool dirty_ = false;          /* out of date / suboptimal / resized: rebuild before next acquire */
        bool resize_pending_ = false; /* WM_SIZE seen: compare surface extent before next acquire */
        bool failed_ = false;
    };

}

#endif
