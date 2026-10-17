#include "render_targets.h"
#include "vk_pipeline.h"

namespace patch
{

    bool RenderTargets::create(VulkanContext &ctx, VkExtent2D size, VkFormat color_format)
    {
        if (!create_point_sampler(ctx.device(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, &sampler))
            return false;

        auto image_2d = [&](VkFormat format, VkImageUsageFlags usage, GpuImage *image,
                            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
        {
            ImageDesc desc;
            desc.format = format;
            desc.extent = {size.width, size.height, 1};
            desc.usage = usage;
            desc.aspect = aspect;
            return ctx.create_image(desc, image);
        };

        /* Raster attachments that the compute G-buffer pass writes and every later pass samples */
        const VkImageUsageFlags gbuffer_usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        const VkImageUsageFlags compute_output = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        for (uint32_t i = 0; i < RASTER_COLOR_COUNT; i++)
        {
            if (!image_2d(RASTER_COLOR_FORMATS[i], gbuffer_usage, &raster_color(i)))
                return false;
        }

        const VkImageUsageFlags drawn_then_sampled = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!image_2d(DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &depth, VK_IMAGE_ASPECT_DEPTH_BIT) ||
            !image_2d(color_format, drawn_then_sampled, &lit_color) ||
            !image_2d(color_format, drawn_then_sampled, &denoised) ||
            !image_2d(VK_FORMAT_R8_UNORM, compute_output, &shadow) ||
            !image_2d(VK_FORMAT_R8_UNORM, compute_output, &ao))
            return false;
        for (int i = 0; i < 2; i++)
        {
            if (!image_2d(VK_FORMAT_R8_UNORM, compute_output, &shadow_history[i]) ||
                !image_2d(VK_FORMAT_R16G16_SFLOAT, compute_output, &ao_history[i]) ||
                !image_2d(TAA_HISTORY_FORMAT, drawn_then_sampled, &taa_history[i]))
                return false;
        }

        /*
         * Sampled before anything writes them: the raw shadow / AO outputs are the lighting
         * pass's inputs until the temporal resolves first run (and the AO input whenever AO is
         * off), and each resolve's first frame binds the "previous" history image.
         */
        VkCommandBuffer cmd = ctx.begin_immediate_commands();
        if (!cmd)
            return false;
        const ImageState read_by_resolve_and_lighting =
            image_use::sampled(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        BarrierBatch barriers(cmd);
        barriers.transition(shadow, read_by_resolve_and_lighting);
        barriers.transition(ao, read_by_resolve_and_lighting);
        for (int i = 0; i < 2; i++)
        {
            barriers.transition(shadow_history[i], read_by_resolve_and_lighting);
            barriers.transition(ao_history[i], read_by_resolve_and_lighting);
            barriers.transition(taa_history[i], image_use::sampled(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT));
        }
        barriers.record();
        if (!ctx.submit_immediate_commands(cmd))
            return false;

        extent = size;
        return true;
    }

    void RenderTargets::destroy(VulkanContext &ctx)
    {
        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
            ctx.destroy_image(&gbuffer[i]);
        ctx.destroy_image(&motion);
        ctx.destroy_image(&depth);
        ctx.destroy_image(&lit_color);
        ctx.destroy_image(&denoised);
        ctx.destroy_image(&shadow);
        ctx.destroy_image(&ao);
        for (int i = 0; i < 2; i++)
        {
            ctx.destroy_image(&shadow_history[i]);
            ctx.destroy_image(&ao_history[i]);
            ctx.destroy_image(&taa_history[i]);
        }
        destroy_sampler(ctx.device(), &sampler);
        extent = {};
    }

}
