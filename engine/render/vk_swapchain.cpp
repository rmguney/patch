#include "vk_swapchain.h"
#include "vk_context.h"
#include "engine/platform/window.h"
#include <cstdio>

namespace patch
{

    /* Shutdown: the longest the present fences of the last frames are waited for */
    static constexpr uint64_t SHUTDOWN_PRESENT_WAIT_NS = 1000000000;

    bool Swapchain::create(VulkanContext &ctx, Window &window)
    {
        ctx_ = &ctx;
        window_ = &window;
        if (ctx.present_fences_supported())
            printf("  Present fences: VK_EXT_swapchain_maintenance1\n");
        return create_swapchain(VK_NULL_HANDLE) && create_present_sync();
    }

    /* The device is idle */
    void Swapchain::destroy()
    {
        if (!ctx_)
            return;
        retire_swapchain_objects(swapchain_);
        swapchain_ = VK_NULL_HANDLE;
        release_retired(true);
    }

    bool Swapchain::query_surface_extent(VkExtent2D *out_extent, VkSurfaceCapabilitiesKHR *out_caps)
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_->physical_device(), ctx_->surface(), &capabilities);
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %d\n", result);
            if (result == VK_ERROR_SURFACE_LOST_KHR || result == VK_ERROR_DEVICE_LOST)
                failed_ = true;
            return false;
        }
        if (out_caps)
            *out_caps = capabilities;

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            /* Surface size is defined by the swapchain: follow the window, clamped to the caps */
            int32_t w = window_->width() > 0 ? window_->width() : 0;
            int32_t h = window_->height() > 0 ? window_->height() : 0;
            extent.width = static_cast<uint32_t>(w);
            extent.height = static_cast<uint32_t>(h);
            if (extent.width < capabilities.minImageExtent.width)
                extent.width = capabilities.minImageExtent.width;
            if (extent.width > capabilities.maxImageExtent.width)
                extent.width = capabilities.maxImageExtent.width;
            if (extent.height < capabilities.minImageExtent.height)
                extent.height = capabilities.minImageExtent.height;
            if (extent.height > capabilities.maxImageExtent.height)
                extent.height = capabilities.maxImageExtent.height;
        }
        *out_extent = extent;
        return true;
    }

    bool Swapchain::create_swapchain(VkSwapchainKHR old_swapchain)
    {
        VkPhysicalDevice physical_device = ctx_->physical_device();
        VkSurfaceKHR surface = ctx_->surface();

        VkSurfaceCapabilitiesKHR capabilities{};
        VkExtent2D extent{};
        if (!query_surface_extent(&extent, &capabilities))
            return false;
        if (extent.width == 0 || extent.height == 0)
        {
            fprintf(stderr, "Cannot create swapchain for a 0x0 surface\n");
            return false;
        }

        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());
        if (formats.empty())
        {
            fprintf(stderr, "Surface reports no formats\n");
            return false;
        }

        /*
         * UNORM swapchain on purpose: deferred_lighting.frag already encodes its output
         * (ACES + pow(1/2.2)), TAA/denoise run on those display-encoded values, and UI
         * colours are authored in sRGB. An *_SRGB swapchain would encode a second time.
         */
        VkSurfaceFormatKHR surface_format = formats[0];
        bool found_format = false;
        const VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
        for (VkFormat want : preferred)
        {
            for (auto &f : formats)
            {
                if (f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    surface_format = f;
                    found_format = true;
                    break;
                }
            }
            if (found_format)
                break;
        }
        if (!found_format)
        {
            fprintf(stderr, "Warning: no UNORM sRGB-nonlinear surface format; using format %d (output may be double-encoded)\n",
                    surface_format.format);
        }

        /* MAILBOX: uncapped frame rate without tearing; FIFO (always supported) otherwise */
        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);
        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data());

        VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR mode : present_modes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
                selected_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        /* Rebuilds pick the same mode and log their own line */
        if (old_swapchain == VK_NULL_HANDLE)
            printf("  Present mode: %s\n",
                   selected_present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX (uncapped)" : "FIFO (vsync)");

        format_ = surface_format.format;
        extent_ = extent;

        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        {
            image_count = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent_;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const uint32_t graphics_family = ctx_->graphics_family();
        const uint32_t present_family = ctx_->present_family();
        uint32_t queue_family_indices[] = {graphics_family, present_family};
        if (graphics_family != present_family)
        {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queue_family_indices;
        }
        else
        {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = selected_present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = old_swapchain;

        VkDevice device = ctx_->device();
        VkResult result = vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain_);
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "vkCreateSwapchainKHR failed: %d (extent: %ux%u)\n",
                    result, extent_.width, extent_.height);
            swapchain_ = VK_NULL_HANDLE;
            return false;
        }

        uint32_t retrieved_count = 0;
        vkGetSwapchainImagesKHR(device, swapchain_, &retrieved_count, nullptr);
        std::vector<VkImage> images(retrieved_count);
        vkGetSwapchainImagesKHR(device, swapchain_, &retrieved_count, images.data());

        images_.assign(retrieved_count, TrackedImage{});
        views_.assign(retrieved_count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < retrieved_count; i++)
        {
            images_[i].image = images[i];
            images_[i].range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = images[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format_;
            view_info.subresourceRange = images_[i].range;

            if (vkCreateImageView(device, &view_info, nullptr, &views_[i]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create swapchain image view %u\n", i);
                return false;
            }
        }

        return true;
    }

    bool Swapchain::create_present_sync()
    {
        VkDevice device = ctx_->device();
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        render_finished_.assign(images_.size(), VK_NULL_HANDLE);
        for (VkSemaphore &semaphore : render_finished_)
        {
            if (vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS)
                return false;
        }

        if (!ctx_->present_fences_supported())
            return true;
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        present_fences_.assign(images_.size(), PresentFence{});
        for (PresentFence &present : present_fences_)
        {
            if (vkCreateFence(device, &fence_info, nullptr, &present.fence) != VK_SUCCESS)
                return false;
        }
        return true;
    }

    void Swapchain::retire_swapchain_objects(VkSwapchainKHR swapchain)
    {
        VkDevice device = ctx_->device();
        for (VkImageView view : views_)
        {
            if (view)
                vkDestroyImageView(device, view, nullptr);
        }
        views_.clear();

        Retired retired;
        retired.swapchain = swapchain;
        retired.semaphores.swap(render_finished_);
        for (PresentFence &present : present_fences_)
        {
            if (present.pending)
                retired.fences.push_back(present.fence);
            else if (present.fence)
                vkDestroyFence(device, present.fence, nullptr);
        }
        present_fences_.clear();
        /* Each image may hold a queued present, and the frames in flight more behind them */
        retired.presents_left = static_cast<uint32_t>(images_.size()) + MAX_FRAMES_IN_FLIGHT;
        images_.clear();

        if (retired.swapchain || !retired.semaphores.empty() || !retired.fences.empty())
            retired_.push_back(std::move(retired));
    }

    void Swapchain::release_retired(bool all)
    {
        VkDevice device = ctx_->device();
        for (size_t i = 0; i < retired_.size();)
        {
            Retired &retired = retired_[i];
            bool done = all || retired.presents_left == 0 || ctx_->present_fences_supported();
            if (!retired.fences.empty())
            {
                /* Polled, or at shutdown waited for a while; an error (device lost) leaves
                 * nothing waiting on the semaphores */
                const VkResult status = vkWaitForFences(device, static_cast<uint32_t>(retired.fences.size()),
                                                        retired.fences.data(), VK_TRUE,
                                                        all ? SHUTDOWN_PRESENT_WAIT_NS : 0);
                done = all || status != VK_TIMEOUT;
            }
            if (!done)
            {
                i++;
                continue;
            }

            for (VkSemaphore semaphore : retired.semaphores)
            {
                if (semaphore)
                    vkDestroySemaphore(device, semaphore, nullptr);
            }
            for (VkFence fence : retired.fences)
                vkDestroyFence(device, fence, nullptr);
            if (retired.swapchain)
                vkDestroySwapchainKHR(device, retired.swapchain, nullptr);
            retired_.erase(retired_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    Swapchain::Status Swapchain::prepare()
    {
        /* A WM_SIZE arrived: only an actual extent change needs a new swapchain */
        if (resize_pending_)
        {
            VkExtent2D extent{};
            if (!query_surface_extent(&extent))
                return Status::Skip;
            if (extent.width == 0 || extent.height == 0)
                return Status::Skip; /* still minimized: keep the check pending, skip the frame */

            resize_pending_ = false;
            if (extent.width != extent_.width || extent.height != extent_.height)
                dirty_ = true; /* a restore or show at the same size rebuilds nothing */
        }

        /* Resized, or out of date / suboptimal from a previous acquire or present */
        return dirty_ ? Status::Rebuild : Status::Ready;
    }

    /*
     * Retires the swapchain into a new one at the surface's current size (oldSwapchain lets the
     * presentation engine hand over without a gap). The images may differ in count and size, so
     * their views and present semaphores are recreated. The caller waited for the device to go
     * idle, so no frame uses the old images; presents may still wait on the old semaphores, which
     * are retired with the old swapchain (release_retired()).
     */
    bool Swapchain::rebuild()
    {
        /* Checked after the caller's wait: the window can be minimized while the GPU drains */
        VkExtent2D extent{};
        if (!query_surface_extent(&extent))
            return false;
        if (extent.width == 0 || extent.height == 0)
            return false; /* minimized: stay dirty and retry on a later frame */

        const VkFormat old_format = format_;
        VkSwapchainKHR old_swapchain = swapchain_;
        swapchain_ = VK_NULL_HANDLE;
        retire_swapchain_objects(old_swapchain); /* alive until released: still the oldSwapchain */

        bool ok = create_swapchain(old_swapchain);

        if (!ok && swapchain_ == VK_NULL_HANDLE && query_surface_extent(&extent) &&
            (extent.width == 0 || extent.height == 0))
        {
            /* Minimized between the check above and the creation: stay dirty and build a
             * fresh swapchain once restored (begin_frame records nothing until then) */
            printf("  Swapchain recreation deferred: surface is 0x0 (minimized)\n");
            return false;
        }

        /* The UI and lighting pipelines were built for the original format */
        if (ok && format_ != old_format)
        {
            fprintf(stderr, "Renderer: swapchain format changed (%d -> %d), not supported\n", old_format, format_);
            ok = false;
        }
        if (ok)
            ok = create_present_sync();

        if (!ok)
        {
            fprintf(stderr, "Renderer: swapchain recreation failed, shutting down\n");
            failed_ = true;
            return false;
        }

        dirty_ = false;
        return true;
    }

    bool Swapchain::acquire(VkSemaphore image_available, uint32_t *image_index)
    {
        VkResult acquire = vkAcquireNextImageKHR(ctx_->device(), swapchain_, UINT64_MAX,
                                                 image_available, VK_NULL_HANDLE, image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
        {
            /* No image acquired and the semaphore is not signaled: skip, rebuild next frame */
            dirty_ = true;
            return false;
        }
        if (acquire == VK_SUBOPTIMAL_KHR)
        {
            /* Image acquired (semaphore will signal): render and present it, rebuild afterwards */
            dirty_ = true;
        }
        else if (acquire != VK_SUCCESS)
        {
            fprintf(stderr, "vkAcquireNextImageKHR failed: %s (%d)\n", vk_result_name(acquire), acquire);
            if (acquire == VK_ERROR_DEVICE_LOST || acquire == VK_ERROR_SURFACE_LOST_KHR ||
                acquire == VK_ERROR_OUT_OF_HOST_MEMORY || acquire == VK_ERROR_OUT_OF_DEVICE_MEMORY)
                failed_ = true;
            return false;
        }

        /* Contents are discarded each frame; the first barrier chains with the semaphore wait */
        images_[*image_index].state = {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_ACCESS_2_NONE};
        return true;
    }

    void Swapchain::present(uint32_t image_index)
    {
        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished_[image_index];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain_;
        present_info.pImageIndices = &image_index;

        /* The image's fence is free again once its previous present is done (long since: the
         * image was acquired again) */
        VkSwapchainPresentFenceInfoEXT fence_info{};
        if (!present_fences_.empty())
        {
            PresentFence &present = present_fences_[image_index];
            if (present.pending)
            {
                const VkResult waited = vkWaitForFences(ctx_->device(), 1, &present.fence, VK_TRUE, UINT64_MAX);
                if (waited != VK_SUCCESS)
                {
                    fprintf(stderr, "Waiting for a present fence failed: %s (%d)\n", vk_result_name(waited), waited);
                    failed_ = true;
                    return;
                }
                vkResetFences(ctx_->device(), 1, &present.fence);
            }
            fence_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
            fence_info.swapchainCount = 1;
            fence_info.pFences = &present.fence;
            present_info.pNext = &fence_info;
            present.pending = true;
        }

        VkResult result = vkQueuePresentKHR(ctx_->present_queue(), &present_info);
        for (Retired &retired : retired_)
        {
            if (retired.presents_left > 0)
                retired.presents_left--;
        }
        release_retired(false);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            dirty_ = true; /* recreated at the start of the next frame */
        }
        else if (result != VK_SUCCESS)
        {
            fprintf(stderr, "vkQueuePresentKHR failed: %s (%d)\n", vk_result_name(result), result);
            if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR)
                failed_ = true;
            else
                dirty_ = true;
        }
    }

}
