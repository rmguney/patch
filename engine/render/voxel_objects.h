#ifndef PATCH_RENDER_VOXEL_OBJECTS_H
#define PATCH_RENDER_VOXEL_OBJECTS_H

#include <vulkan/vulkan.h>
#include "engine/core/math.h"
#include "engine/render/frame_resources.h"
#include "engine/render/raster_gpu_types.h"
#include "engine/render/terrain_volume.h"
#include "engine/render/voxel_bricks.h"
#include "engine/voxel/bvh.h"
#include "engine/voxel/voxel_object.h"
#include <cstdint>
#include <vector>

namespace patch
{

    /*
     * Voxel bricks of every object, sampled by the object fragment shader and the shadow trace:
     * a BRICK_ATLAS_SIZE^3 image of BRICK_ATLAS_CAPACITY bricks (voxel_bricks.h). Bricks go
     * through a staging buffer with MAX_BRICK_UPLOADS_PER_FRAME bricks per frame in flight.
     */
    class VoxelObjectAtlas
    {
    public:
        /* The bytes of 8 whole grids per frame; any object's bricks fit one frame */
        static constexpr uint32_t MAX_BRICK_UPLOADS_PER_FRAME = 512;
        static_assert(MAX_BRICK_UPLOADS_PER_FRAME >= VOBJ_BRICK_COUNT, "objects are uploaded whole");

        bool create(VulkanContext &ctx);
        void destroy();

        VkImageView view() const { return image_.view; }
        VkSampler sampler() const { return sampler_; }

        /* Frame slot `slot`'s staging bricks (VOBJ_BRICK_VOXELS bytes each), free again once
         * that slot's fence was waited */
        uint8_t *staging(uint32_t slot) const;

        /* Records the copy of the first `count` staged bricks of frame slot `slot` into atlas
         * bricks atlas_bricks[0, count) */
        void record_upload(VkCommandBuffer cmd, uint32_t slot, const uint16_t *atlas_bricks, uint32_t count);

    private:
        VulkanContext *ctx_ = nullptr;
        GpuImage image_;
        VkSampler sampler_ = VK_NULL_HANDLE;
        VulkanBuffer staging_{};
        std::vector<VkBufferImageCopy> regions_;
    };

    /*
     * Raymarched voxel objects: the brick atlas, per-frame metadata and BVH (rewritten by the CPU
     * every frame), and the pipelines that draw one proxy cube per object into the G-buffer (one
     * for the cubes the camera is outside of, one for those it is inside of; raster_gpu_types.h).
     * The bricks an object's voxel changes touched are re-uploaded (ObjectBricks).
     */
    class VoxelObjects
    {
    public:
        bool create(VulkanContext &ctx);
        void destroy();

        /* Per scene; needs the terrain's material palette (TerrainVolume::begin_scene) */
        bool create_scene(uint32_t max_objects, const TerrainVolume &terrain, const FrameResources &frames);
        /* Device idle. Safe on a partially created scene. */
        void destroy_scene();
        bool ready() const { return ready_; }
        uint32_t capacity() const { return capacity_; }

        /* Forget which bricks the atlas holds (the next world is uploaded from scratch) */
        void reset_tracking();

        /* Uploads changed bricks and writes this frame's metadata and BVH (pc: the camera this
         * frame is drawn from), every object at its pose alpha of a tick past its previous one
         * (voxel_object_draw_pose). False when the brick upload could not be submitted. */
        bool update(const FrameContext &frame, const VoxelObjectWorld *world, float alpha, const VoxelObjectPushConstants &pc);

        /* Draws the objects of this frame's metadata into the open G-buffer rendering scope */
        void draw(const FrameContext &frame, VoxelObjectPushConstants pc) const;

        /* Objects in this frame's metadata buffer (active and hidden) */
        int32_t gpu_object_count() const { return total_count_; }

        VkSampler atlas_sampler() const { return atlas_.sampler(); }
        VkImageView atlas_view() const { return atlas_.view(); }
        VkBuffer metadata(uint32_t slot) const { return metadata_[slot].buffer; }
        VkBuffer bvh(uint32_t slot) const { return bvh_[slot].buffer; }

    private:
        /* The transform an object slot was drawn with last frame, and the object it belonged to */
        struct SlotMotion
        {
            float local_to_world[16] = {};
            uint32_t alloc_id = 0;
            uint32_t voxel_revision = 0;
            bool valid = false;
        };

        bool upload_changed_bricks(const FrameContext &frame, const VoxelObjectWorld *world, uint32_t count);
        void write_metadata(uint32_t slot, const VoxelObjectWorld *world, uint32_t count, float alpha,
                            const VoxelObjectPushConstants &pc);

        VulkanContext *ctx_ = nullptr;

        VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;        /* front faces: cubes the camera is outside of */
        VkPipeline inside_pipeline_ = VK_NULL_HANDLE; /* back faces: cubes the camera is inside of */
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkDescriptorSet sets_[MAX_FRAMES_IN_FLIGHT] = {};
        /* Submitted ahead of the frame's command buffer; slot i is reused after fence i */
        VkCommandBuffer upload_cmd_[MAX_FRAMES_IN_FLIGHT] = {};

        VoxelObjectAtlas atlas_;
        VulkanBuffer metadata_[MAX_FRAMES_IN_FLIGHT] = {};
        VulkanBuffer bvh_[MAX_FRAMES_IN_FLIGHT] = {};
        BVH *cpu_bvh_ = nullptr;
        uint32_t capacity_ = 0; /* metadata slots */
        bool ready_ = false;

        ObjectBricks bricks_;
        uint16_t staged_bricks_[VoxelObjectAtlas::MAX_BRICK_UPLOADS_PER_FRAME] = {};
        std::vector<SlotMotion> motion_; /* per metadata slot */
        Vec3 draw_positions_[VOBJ_MAX_OBJECTS] = {}; /* this frame's, by object index: the BVH bounds them */
        int32_t total_count_ = 0;
        bool camera_near_ = false; /* some cube may hold the camera: the inside draw runs */
        const VoxelObjectWorld *last_world_ = nullptr;
    };

}

#endif
