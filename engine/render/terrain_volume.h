#ifndef PATCH_RENDER_TERRAIN_VOLUME_H
#define PATCH_RENDER_TERRAIN_VOLUME_H

#include <vulkan/vulkan.h>
#include "engine/render/vk_context.h"
#include "engine/core/jobs.h"
#include "engine/voxel/volume.h"
#include <cstdint>
#include <vector>

namespace patch
{

    /* Material data for GPU upload (matches GPUMaterialColor layout) */
    struct MaterialEntry
    {
        float r, g, b, emissive;
        float roughness, metallic, flags, pad;
    };

    /* Terrain volume parameters, cached per scene */
    struct VolumeParams
    {
        float bounds_min[3];
        float bounds_max[3];
        float voxel_size;
        int32_t grid_size[3];
        int32_t chunks_dim[3];
        int32_t total_chunks;
    };

    /*
     * The scene's terrain on the GPU: chunk voxels and occupancy headers for the G-buffer
     * raymarch, the material palette, and the half-resolution shadow volume (opaque voxels,
     * VOLUME_SHADOW_MIPS mips) the shadow and AO passes trace and the G-buffer raymarch crosses
     * water with. Chunk edits reach the GPU through per-frame upload command buffers,
     * the repacked parts of the shadow volume (boxes of edited chunks, every mip) through
     * triple-buffered async uploads; both are submitted ahead of the frame's own command buffer.
     *
     * A scene's terrain arrives in steps, so that its CPU work runs on other threads while a
     * loading screen keeps presenting: begin_scene() (presenting thread) creates the buffers
     * and the staging of the upload's slices, pack_shadow_volume() and stage_chunks() (one
     * other thread, with the job system; no Vulkan calls) fill it, and poll_scene(), once per
     * loading screen frame (presenting thread), submits a slice until it reports the upload
     * done and the terrain ready. Only the chunks that hold voxels are read and copied: the
     * GPU zero-fills the rest.
     */
    class TerrainVolume
    {
    public:
        static constexpr int32_t RAYMARCH_MIN_STEPS = 512;

        bool create(VulkanContext &ctx);
        void destroy();

        enum class SceneUpload : uint8_t
        {
            Pending,
            Done,
            Failed
        };

        bool begin_scene(VoxelVolume *vol);
        void pack_shadow_volume(JobSystem *jobs);
        void stage_chunks(JobSystem *jobs);
        SceneUpload poll_scene();

        /* Device idle, and no pack_shadow_volume() or stage_chunks() running. Safe on a
         * partially created scene. */
        void destroy_scene();
        bool ready() const { return ready_; }

        void set_material_palette(const MaterialEntry *materials, int32_t count);

        /* Uploads the dirty chunks; returns how many indices it wrote, or -1 when the upload
         * could not be submitted (device lost) */
        int32_t upload_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_indices, uint32_t slot);
        /* Repacks and uploads the edited chunks. False when the upload could not be submitted. */
        bool update_shadow_volume(VoxelVolume *vol);

        const VolumeParams &params() const { return params_; }
        int32_t raymarch_max_steps() const { return raymarch_max_steps_; }

        VkBuffer voxel_data() const { return voxel_data_.buffer; }
        VkBuffer chunk_headers() const { return chunk_headers_.buffer; }
        VkBuffer material_palette() const { return material_palette_.buffer; }
        VkImageView shadow_volume_view() const { return shadow_volume_.view; }
        VkSampler shadow_volume_sampler() const { return shadow_volume_sampler_; }

    private:
        static constexpr uint32_t SHADOW_UPLOAD_BUFFERS = 3;
        /* One update hands over at most VOLUME_SHADOW_DIRTY_MAX chunks, boxed without overlap */
        static constexpr VkDeviceSize SHADOW_STAGING_SIZE = VkDeviceSize{VOLUME_SHADOW_DIRTY_MAX} * VOLUME_SHADOW_CHUNK_BYTES;
        static constexpr uint32_t SHADOW_MAX_REGIONS = VOLUME_SHADOW_MIPS * VOLUME_SHADOW_DIRTY_MAX;

        /* Whether each material blocks light (the shadow volume holds the voxels that do) */
        void material_occlusion(bool occludes[VOXEL_MATERIAL_MAX]) const;
        bool upload_shadow_boxes(const VoxelVolume *vol, const uint8_t *const mips[VOLUME_SHADOW_MIPS], const ChunkBox *boxes,
                                 int32_t box_count);
        void record_shadow_copy(VkCommandBuffer cmd, VkBuffer staging, uint32_t region_count);
        bool plan_scene_upload();
        bool submit_upload_slice();
        void finish_upload_slot(uint32_t index);
        void wait_for_shadow_upload(uint32_t index);
        void wait_all_shadow_uploads();

        VulkanContext *ctx_ = nullptr;

        MaterialEntry material_entries_[VOXEL_MATERIAL_MAX] = {};
        int32_t material_count_ = 0;

        VolumeParams params_{};
        int32_t raymarch_max_steps_ = RAYMARCH_MIN_STEPS;
        bool ready_ = false;

        VulkanBuffer voxel_data_{};       /* SSBO: chunk voxel material IDs */
        VulkanBuffer chunk_headers_{};    /* SSBO: chunk occupancy headers */
        VulkanBuffer material_palette_{}; /* UBO */

        /* Persistent staging for chunk uploads: one region per frame in flight (region i is only
         * rewritten after frame slot i's fence has been waited) */
        VulkanBuffer staging_voxels_{};
        VulkanBuffer staging_headers_{};

        VkDeviceSize staging_voxels_frame_size_ = 0;
        VkDeviceSize staging_headers_frame_size_ = 0;
        VkCommandBuffer upload_cmd_[MAX_FRAMES_IN_FLIGHT] = {};

        GpuImage shadow_volume_;
        VkSampler shadow_volume_sampler_ = VK_NULL_HANDLE;
        std::vector<uint8_t> shadow_mips_[VOLUME_SHADOW_MIPS]; /* CPU copy; empty: no scene packed yet */
        std::vector<ShadowCopyRegion> shadow_regions_;          /* SHADOW_MAX_REGIONS, reused every upload */
        std::vector<VkBufferImageCopy> shadow_copies_;

        VkFence shadow_upload_fences_[SHADOW_UPLOAD_BUFFERS] = {};
        VulkanBuffer shadow_staging_buffers_[SHADOW_UPLOAD_BUFFERS] = {}; /* SHADOW_STAGING_SIZE each */
        VkCommandBuffer shadow_upload_cmds_[SHADOW_UPLOAD_BUFFERS] = {};
        bool shadow_upload_pending_[SHADOW_UPLOAD_BUFFERS] = {};
        uint32_t shadow_upload_index_ = 0;

        /* A loading screen frame's share of a scene's upload: about 3 ms over PCIe 4 */
        static constexpr VkDeviceSize SCENE_UPLOAD_SLICE = VkDeviceSize{64} << 20;
        /* A chunk's GPU data is a byte per voxel */
        static constexpr size_t CHUNKS_PER_SLICE = static_cast<size_t>(SCENE_UPLOAD_SLICE / CHUNK_VOXEL_COUNT);

        /* One copy of a scene's upload, from its slice's staging */
        struct SceneUploadOp
        {
            VkBuffer dst_buffer; /* VK_NULL_HANDLE: image_copy into the shadow volume */
            VkBufferCopy buffer_copy;
            VkBufferImageCopy image_copy;
        };
        struct SceneSlice
        {
            VulkanBuffer staging; /* freed once the slice completed */
            VkDeviceSize bytes = 0;
            std::vector<SceneUploadOp> ops;
        };

        /* The scene being loaded (begin_scene() to poll_scene()): its chunks that hold voxels,
         * in volume order, and its upload: the slices of those chunks, then one of every
         * chunk's header, then those of the shadow volume */
        VoxelVolume *loading_volume_ = nullptr;
        std::vector<int32_t> filled_chunks_;
        std::vector<SceneSlice> scene_slices_;
        size_t header_slice_ = 0;
        size_t first_shadow_slice_ = 0;
        size_t next_slice_ = 0;                                   /* the next to submit */
        int32_t slot_slice_[SHADOW_UPLOAD_BUFFERS] = {-1, -1, -1}; /* the slice each upload slot carries */
    };

}

#endif
