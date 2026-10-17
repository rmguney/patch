#ifndef PATCH_RENDER_GBUFFER_PASS_H
#define PATCH_RENDER_GBUFFER_PASS_H

#include <vulkan/vulkan.h>
#include "engine/render/frame_resources.h"
#include "engine/render/render_targets.h"
#include "engine/render/terrain_volume.h"
#include "engine/render/voxel_push_constants.h"

namespace patch
{

    /*
     * Fills the G-buffer: the compute raymarch of the terrain writes it and the motion vectors;
     * when there are voxel objects or particles to draw, the depth prime turns its linear depth
     * into hardware depth and the raster pass lets their pipelines (built against
     * RASTER_COLOR_FORMATS) draw on top.
     */
    class GBufferPass
    {
    public:
        bool create(VulkanContext &ctx);
        void destroy();

        void bind_targets(const RenderTargets &targets);
        void bind_scene(const TerrainVolume &terrain, const FrameResources &frames);

        void trace_terrain(const FrameContext &frame, RenderTargets &targets, const VoxelPushConstants &pc);

        /* After trace_terrain(): primes the depth buffer with the traced linear depth, so the
         * raster pipelines depth-test against the terrain, and opens the rendering scope that
         * loads the traced G-buffer */
        void begin_raster(const FrameContext &frame, RenderTargets &targets, float near_plane, float far_plane);

        /* Hands the G-buffer and motion vectors to the shading passes, ending the raster scope
         * when begin_raster() opened one */
        void finish(const FrameContext &frame, RenderTargets &targets, bool rasterized);

    private:
        void prime_depth(const FrameContext &frame, RenderTargets &targets, float near_plane, float far_plane);

        VulkanContext *ctx_ = nullptr;

        VkDescriptorSetLayout terrain_layout_ = VK_NULL_HANDLE; /* set 0: chunks, palette, temporal UBO */
        VkDescriptorSetLayout output_layout_ = VK_NULL_HANDLE;  /* set 1: G-buffer + motion vector storage */
        VkPipelineLayout trace_layout_ = VK_NULL_HANDLE;
        VkPipeline trace_pipeline_ = VK_NULL_HANDLE;

        VkDescriptorSetLayout depth_prime_set_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout depth_prime_layout_ = VK_NULL_HANDLE;
        VkPipeline depth_prime_pipeline_ = VK_NULL_HANDLE;

        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet terrain_sets_[MAX_FRAMES_IN_FLIGHT] = {};
        VkDescriptorSet output_set_ = VK_NULL_HANDLE;
        VkDescriptorSet depth_prime_set_ = VK_NULL_HANDLE;
    };

}

#endif
