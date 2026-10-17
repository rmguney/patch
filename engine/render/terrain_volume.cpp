#include "terrain_volume.h"
#include "gpu_volume.h"
#include "vk_pipeline.h"
#include "engine/core/profile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace patch
{

    /* Every reader is a compute pass: the G-buffer raymarch reads the chunk buffers, it and the
     * shadow and AO passes trace the shadow volume */
    static constexpr VkPipelineStageFlags2 READER_STAGES = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    static constexpr GpuAccess CHUNK_READ{READER_STAGES, VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    static constexpr GpuAccess COPY_WRITE{VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};

    static size_t shadow_mip_size(const VoxelVolume *vol, uint32_t level)
    {
        uint32_t dims[3];
        volume_shadow_mip_dims(vol, level, dims);
        return static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    }

    static void begin_one_time(VkCommandBuffer cmd)
    {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);
    }

    bool TerrainVolume::create(VulkanContext &ctx)
    {
        ctx_ = &ctx;

        /* Shadow uploads: one staging buffer, command buffer and fence each, reused for every upload */
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        for (uint32_t i = 0; i < SHADOW_UPLOAD_BUFFERS; i++)
        {
            if (vkCreateFence(ctx.device(), &fence_info, nullptr, &shadow_upload_fences_[i]) != VK_SUCCESS ||
                !ctx.create_buffer(SHADOW_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &shadow_staging_buffers_[i]))
                return false;
        }
        shadow_regions_.resize(SHADOW_MAX_REGIONS);
        shadow_copies_.resize(SHADOW_MAX_REGIONS);
        return ctx.allocate_command_buffers(MAX_FRAMES_IN_FLIGHT, upload_cmd_) &&
               ctx.allocate_command_buffers(SHADOW_UPLOAD_BUFFERS, shadow_upload_cmds_) &&
               create_point_sampler(ctx.device(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                    static_cast<float>(VOLUME_SHADOW_MIPS - 1), &shadow_volume_sampler_);
    }

    void TerrainVolume::destroy()
    {
        if (!ctx_)
            return;
        destroy_scene();
        for (uint32_t i = 0; i < SHADOW_UPLOAD_BUFFERS; i++)
        {
            if (shadow_upload_fences_[i] != VK_NULL_HANDLE)
                vkDestroyFence(ctx_->device(), shadow_upload_fences_[i], nullptr);
            shadow_upload_fences_[i] = VK_NULL_HANDLE;
            ctx_->destroy_buffer(&shadow_staging_buffers_[i]);
        }
        destroy_sampler(ctx_->device(), &shadow_volume_sampler_);
    }

    void TerrainVolume::set_material_palette(const MaterialEntry *materials, int32_t count)
    {
        material_count_ = count < VOXEL_MATERIAL_MAX ? count : VOXEL_MATERIAL_MAX;
        memcpy(material_entries_, materials, material_count_ * sizeof(MaterialEntry));
    }

    bool TerrainVolume::begin_scene(VoxelVolume *vol)
    {
        /* Raymarch step budget from the volume diagonal */
        {
            float gx = static_cast<float>(vol->chunks_x * CHUNK_SIZE);
            float gy = static_cast<float>(vol->chunks_y * CHUNK_SIZE);
            float gz = static_cast<float>(vol->chunks_z * CHUNK_SIZE);
            float diag = sqrtf(gx * gx + gy * gy + gz * gz);
            raymarch_max_steps_ = static_cast<int32_t>(diag * 1.1f);
            if (raymarch_max_steps_ < RAYMARCH_MIN_STEPS)
                raymarch_max_steps_ = RAYMARCH_MIN_STEPS;
        }

        params_.bounds_min[0] = vol->bounds.min_x;
        params_.bounds_min[1] = vol->bounds.min_y;
        params_.bounds_min[2] = vol->bounds.min_z;
        params_.bounds_max[0] = vol->bounds.max_x;
        params_.bounds_max[1] = vol->bounds.max_y;
        params_.bounds_max[2] = vol->bounds.max_z;
        params_.voxel_size = vol->voxel_size;
        params_.grid_size[0] = vol->chunks_x * CHUNK_SIZE;
        params_.grid_size[1] = vol->chunks_y * CHUNK_SIZE;
        params_.grid_size[2] = vol->chunks_z * CHUNK_SIZE;
        params_.chunks_dim[0] = vol->chunks_x;
        params_.chunks_dim[1] = vol->chunks_y;
        params_.chunks_dim[2] = vol->chunks_z;
        params_.total_chunks = vol->total_chunks;

        uint32_t shadow_dims[3];
        volume_shadow_mip_dims(vol, 0, shadow_dims);
        if (shadow_mip_size(vol, 0) == 0)
            return false;

        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const VkDeviceSize voxel_data_size = static_cast<VkDeviceSize>(vol->total_chunks) * GPU_CHUNK_DATA_SIZE;
        const VkDeviceSize headers_size = static_cast<VkDeviceSize>(vol->total_chunks) * sizeof(GPUChunkHeader);

        /* One staging region per frame in flight: region N is rewritten only after frame slot N's
         * fence was waited, so the CPU never overwrites data a pending copy still reads */
        staging_voxels_frame_size_ = static_cast<VkDeviceSize>(VOLUME_MAX_UPLOADS_PER_FRAME) * GPU_CHUNK_DATA_SIZE;
        staging_headers_frame_size_ = static_cast<VkDeviceSize>(VOLUME_MAX_UPLOADS_PER_FRAME) * sizeof(GPUChunkHeader);

        ImageDesc shadow_desc;
        shadow_desc.type = VK_IMAGE_TYPE_3D;
        shadow_desc.format = VK_FORMAT_R8_UINT;
        shadow_desc.extent = {shadow_dims[0], shadow_dims[1], shadow_dims[2]};
        shadow_desc.mip_levels = VOLUME_SHADOW_MIPS;
        shadow_desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        if (!ctx_->create_buffer(voxel_data_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &voxel_data_) ||
            !ctx_->create_buffer(headers_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &chunk_headers_) ||
            !ctx_->create_buffer(sizeof(GPUMaterialPalette), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, &material_palette_) ||
            !ctx_->create_buffer(staging_voxels_frame_size_ * MAX_FRAMES_IN_FLIGHT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 host, &staging_voxels_) ||
            !ctx_->create_buffer(staging_headers_frame_size_ * MAX_FRAMES_IN_FLIGHT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 host, &staging_headers_) ||
            !ctx_->create_image(shadow_desc, &shadow_volume_))
            return false;

        GPUMaterialPalette *palette = static_cast<GPUMaterialPalette *>(material_palette_.mapped);
        memset(palette, 0, sizeof(GPUMaterialPalette));
        for (int32_t i = 0; i < material_count_ && i < GPU_MATERIAL_PALETTE_SIZE; i++)
        {
            GPUMaterialColor &c = palette->colors[i];
            c.r = material_entries_[i].r;
            c.g = material_entries_[i].g;
            c.b = material_entries_[i].b;
            c.emissive = material_entries_[i].emissive;
            c.roughness = material_entries_[i].roughness;
            c.metallic = material_entries_[i].metallic;
            c.flags = material_entries_[i].flags;
        }

        /* The chunks to copy; an empty chunk's voxels are never touched, so its memory need not
         * even be resident. Its occupancy is (the generation kept the counts with it). */
        filled_chunks_.clear();
        for (int32_t ci = 0; ci < vol->total_chunks; ci++)
        {
            if (vol->chunks[ci].occupancy.has_any)
                filled_chunks_.push_back(ci);
        }
        loading_volume_ = vol;
        if (!plan_scene_upload())
            return false;
        printf("  Shadow volume created: %ux%ux%u (%d mip levels); %zu of %d chunks hold voxels, %zu upload slices\n",
               shadow_dims[0], shadow_dims[1], shadow_dims[2], VOLUME_SHADOW_MIPS, filled_chunks_.size(),
               vol->total_chunks, scene_slices_.size());
        return true;
    }

    /*
     * The upload as slices of at most SCENE_UPLOAD_SLICE bytes, each with a staging buffer of its
     * own: the filled chunks, CHUNKS_PER_SLICE to a slice, copied in runs of consecutive ones;
     * every header; the shadow volume mip by mip, in slabs of z slices. Its staging is laid out
     * as the ops read it, in op order.
     */
    bool TerrainVolume::plan_scene_upload()
    {
        scene_slices_.clear();
        next_slice_ = 0;

        for (size_t first = 0; first < filled_chunks_.size(); first += CHUNKS_PER_SLICE)
        {
            SceneSlice &slice = scene_slices_.emplace_back();
            const size_t end = std::min(first + CHUNKS_PER_SLICE, filled_chunks_.size());
            size_t run = first;
            for (size_t i = first + 1; i <= end; i++)
            {
                if (i < end && filled_chunks_[i] == filled_chunks_[i - 1] + 1)
                    continue;
                SceneUploadOp op{};
                op.dst_buffer = voxel_data_.buffer;
                op.buffer_copy = {static_cast<VkDeviceSize>(run - first) * GPU_CHUNK_DATA_SIZE,
                                  static_cast<VkDeviceSize>(filled_chunks_[run]) * GPU_CHUNK_DATA_SIZE,
                                  static_cast<VkDeviceSize>(i - run) * GPU_CHUNK_DATA_SIZE};
                slice.ops.push_back(op);
                run = i;
            }
            slice.bytes = static_cast<VkDeviceSize>(end - first) * GPU_CHUNK_DATA_SIZE;
        }

        header_slice_ = scene_slices_.size();
        {
            SceneSlice &slice = scene_slices_.emplace_back();
            SceneUploadOp op{};
            op.dst_buffer = chunk_headers_.buffer;
            op.buffer_copy = {0, 0, static_cast<VkDeviceSize>(params_.total_chunks) * sizeof(GPUChunkHeader)};
            slice.ops.push_back(op);
            slice.bytes = op.buffer_copy.size;
        }

        first_shadow_slice_ = scene_slices_.size();
        const VoxelVolume *vol = loading_volume_;
        for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
        {
            uint32_t dims[3];
            volume_shadow_mip_dims(vol, level, dims);
            const VkDeviceSize layer = static_cast<VkDeviceSize>(dims[0]) * dims[1];
            const uint32_t slab = static_cast<uint32_t>(std::max<VkDeviceSize>(1, SCENE_UPLOAD_SLICE / layer));
            for (uint32_t z = 0; z < dims[2]; z += slab)
            {
                const uint32_t depth = std::min(slab, dims[2] - z);
                const VkDeviceSize bytes = layer * depth;
                if (scene_slices_.size() == first_shadow_slice_ || scene_slices_.back().bytes + bytes > SCENE_UPLOAD_SLICE)
                    scene_slices_.emplace_back();
                SceneSlice &slice = scene_slices_.back();
                SceneUploadOp op{};
                op.image_copy.bufferOffset = slice.bytes;
                op.image_copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                op.image_copy.imageOffset = {0, 0, static_cast<int32_t>(z)};
                op.image_copy.imageExtent = {dims[0], dims[1], depth};
                slice.ops.push_back(op);
                slice.bytes += bytes;
            }
        }

        /* Memory of their own: freed slice by slice, it leaves the process with them */
        for (SceneSlice &slice : scene_slices_)
        {
            if (!ctx_->create_buffer(slice.bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &slice.staging,
                                     true))
                return false;
        }
        return true;
    }

    namespace
    {
        /* pack_shadow_volume(): one filled chunk per index */
        struct ShadowPackJob
        {
            const VoxelVolume *vol;
            const bool *occludes;
            const int32_t *chunks;
            uint8_t *mips[VOLUME_SHADOW_MIPS];
            uint32_t dims0[3];
        };

        /* A chunk's mip0 texels and its texels of every mip above lie in no other chunk */
        void pack_shadow_chunks(void *ctx, int32_t begin, int32_t end)
        {
            const ShadowPackJob *job = static_cast<const ShadowPackJob *>(ctx);
            const VoxelVolume *vol = job->vol;
            for (int32_t i = begin; i < end; i++)
            {
                const int32_t ci = job->chunks[i];
                volume_pack_shadow_chunk(vol, job->occludes, ci, job->mips[0], job->dims0[0], job->dims0[1], job->dims0[2]);
                const int32_t cx = ci % vol->chunks_x;
                const int32_t cy = ci / vol->chunks_x % vol->chunks_y;
                const int32_t cz = ci / (vol->chunks_x * vol->chunks_y);
                const ChunkBox box = {cx, cy, cz, cx, cy, cz};
                volume_shadow_build_mips(vol, &box, job->mips);
            }
        }

        /* A contiguous copy in pieces of STAGE_COPY_BYTES */
        constexpr size_t STAGE_COPY_BYTES = size_t{1} << 20;

        struct StageCopyJob
        {
            const uint8_t *src;
            uint8_t *dst;
            size_t size;
        };

        void stage_copy(void *ctx, int32_t begin, int32_t end)
        {
            const StageCopyJob *job = static_cast<const StageCopyJob *>(ctx);
            const size_t from = static_cast<size_t>(begin) * STAGE_COPY_BYTES;
            const size_t to = std::min(static_cast<size_t>(end) * STAGE_COPY_BYTES, job->size);
            memcpy(job->dst + from, job->src + from, to - from);
        }

        /* stage_chunks(): one filled chunk per index (voxels), one chunk per index (headers) */
        struct ChunkStageJob
        {
            const VoxelVolume *vol;
            const int32_t *chunks;
            uint8_t *const *slices; /* staging of the chunk slices */
            int32_t chunks_per_slice;
            GPUChunkHeader *headers;
        };

        void stage_chunk_voxels(void *ctx, int32_t begin, int32_t end)
        {
            const ChunkStageJob *job = static_cast<const ChunkStageJob *>(ctx);
            for (int32_t i = begin; i < end; i++)
            {
                uint8_t *dst = job->slices[i / job->chunks_per_slice] +
                               static_cast<size_t>(i % job->chunks_per_slice) * GPU_CHUNK_DATA_SIZE;
                gpu_chunk_copy_voxels(&job->vol->chunks[job->chunks[i]], dst);
            }
        }

        void stage_chunk_headers(void *ctx, int32_t begin, int32_t end)
        {
            const ChunkStageJob *job = static_cast<const ChunkStageJob *>(ctx);
            for (int32_t ci = begin; ci < end; ci++)
                job->headers[ci] = gpu_chunk_header_from_chunk(&job->vol->chunks[ci]);
        }
    }

    void TerrainVolume::material_occlusion(bool occludes[VOXEL_MATERIAL_MAX]) const
    {
        /* Transparent materials (water) let sun and sky light through: the lighting pass
         * attenuates it with depth instead. Same rule as is_material_transparent() in
         * shaders/include/materials.glsl; ids past the palette count as opaque. */
        constexpr float k_transparent_flags_min = 16.0f;
        for (int32_t i = 0; i < VOXEL_MATERIAL_MAX; i++)
            occludes[i] = i >= material_count_ || material_entries_[i].flags < k_transparent_flags_min;
    }

    /* The whole shadow volume and its mips, chunk by chunk on the job system, into the CPU copy
     * that later edits repack, and from there into the staging of the shadow slices */
    void TerrainVolume::pack_shadow_volume(JobSystem *jobs)
    {
        VoxelVolume *vol = loading_volume_;
        PROFILE_BEGIN(PROFILE_SHADOW_TERRAIN_PACK);
        bool occludes[VOXEL_MATERIAL_MAX];
        material_occlusion(occludes);

        ShadowPackJob pack{};
        pack.vol = vol;
        pack.occludes = occludes;
        pack.chunks = filled_chunks_.data();
        for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
        {
            shadow_mips_[level].assign(shadow_mip_size(vol, level), 0);
            pack.mips[level] = shadow_mips_[level].data();
        }
        volume_shadow_mip_dims(vol, 0, pack.dims0);
        job_parallel_for(jobs, static_cast<int32_t>(filled_chunks_.size()), 0, pack_shadow_chunks, &pack);
        PROFILE_END(PROFILE_SHADOW_TERRAIN_PACK);

        /* Each op's slab is a contiguous range of its mip */
        for (size_t s = first_shadow_slice_; s < scene_slices_.size(); s++)
        {
            const SceneSlice &slice = scene_slices_[s];
            for (const SceneUploadOp &op : slice.ops)
            {
                const VkBufferImageCopy &c = op.image_copy;
                const size_t layer = static_cast<size_t>(c.imageExtent.width) * c.imageExtent.height;
                StageCopyJob copy = {shadow_mips_[c.imageSubresource.mipLevel].data() + layer * static_cast<size_t>(c.imageOffset.z),
                                     static_cast<uint8_t *>(slice.staging.mapped) + c.bufferOffset,
                                     layer * c.imageExtent.depth};
                job_parallel_for(jobs, static_cast<int32_t>((copy.size + STAGE_COPY_BYTES - 1) / STAGE_COPY_BYTES), 1,
                                 stage_copy, &copy);
            }
        }

        /* Edits are tracked from here on */
        volume_clear_shadow_dirty(vol);
    }

    void TerrainVolume::stage_chunks(JobSystem *jobs)
    {
        std::vector<uint8_t *> slices(header_slice_);
        for (size_t s = 0; s < header_slice_; s++)
            slices[s] = static_cast<uint8_t *>(scene_slices_[s].staging.mapped);
        ChunkStageJob stage = {loading_volume_, filled_chunks_.data(), slices.data(), static_cast<int32_t>(CHUNKS_PER_SLICE),
                               static_cast<GPUChunkHeader *>(scene_slices_[header_slice_].staging.mapped)};
        job_parallel_for(jobs, static_cast<int32_t>(filled_chunks_.size()), 0, stage_chunk_voxels, &stage);
        job_parallel_for(jobs, loading_volume_->total_chunks, 0, stage_chunk_headers, &stage);
    }

    /* Waits for upload slot index and frees the staging of the scene slice it carried */
    void TerrainVolume::finish_upload_slot(uint32_t index)
    {
        wait_for_shadow_upload(index);
        if (slot_slice_[index] >= 0)
            ctx_->destroy_buffer(&scene_slices_[static_cast<size_t>(slot_slice_[index])].staging);
        slot_slice_[index] = -1;
    }

    /* The next slice, in the next shadow upload slot (whose previous slice, two ago, has
     * completed by now) */
    bool TerrainVolume::submit_upload_slice()
    {
        const uint32_t idx = shadow_upload_index_;
        shadow_upload_index_ = (shadow_upload_index_ + 1) % SHADOW_UPLOAD_BUFFERS;
        finish_upload_slot(idx);
        VkCommandBuffer cmd = shadow_upload_cmds_[idx];
        begin_one_time(cmd);

        BarrierBatch barriers(cmd);
        if (next_slice_ == 0)
        {
            /* The chunks without voxels stay zero */
            static constexpr GpuAccess FILL_WRITE{VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
            vkCmdFillBuffer(cmd, voxel_data_.buffer, 0, VK_WHOLE_SIZE, 0);
            barriers.buffer(voxel_data_.buffer, FILL_WRITE, COPY_WRITE);
            barriers.discard(shadow_volume_, image_use::copy_dst);
            barriers.record();
        }

        const SceneSlice &slice = scene_slices_[next_slice_];
        for (const SceneUploadOp &op : slice.ops)
        {
            if (op.dst_buffer != VK_NULL_HANDLE)
                vkCmdCopyBuffer(cmd, slice.staging.buffer, op.dst_buffer, 1, &op.buffer_copy);
            else
                vkCmdCopyBufferToImage(cmd, slice.staging.buffer, shadow_volume_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                       &op.image_copy);
        }
        slot_slice_[idx] = static_cast<int32_t>(next_slice_);
        next_slice_++;

        if (next_slice_ == scene_slices_.size())
        {
            barriers.buffer(voxel_data_.buffer, COPY_WRITE, CHUNK_READ);
            barriers.buffer(chunk_headers_.buffer, COPY_WRITE, CHUNK_READ);
            barriers.transition(shadow_volume_, image_use::sampled(READER_STAGES));
            barriers.record();
        }

        VkResult result = vkEndCommandBuffer(cmd);
        if (result == VK_SUCCESS)
        {
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            result = vkQueueSubmit(ctx_->graphics_queue(), 1, &submit_info, shadow_upload_fences_[idx]);
        }
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Terrain upload failed: %s (%d)\n", vk_result_name(result), result);
            return false;
        }
        shadow_upload_pending_[idx] = true;
        return true;
    }

    TerrainVolume::SceneUpload TerrainVolume::poll_scene()
    {
        /* The staging of a completed slice is freed, one per call: freeing it all at once would
         * hold the driver up for tens of milliseconds */
        for (uint32_t i = 0; i < SHADOW_UPLOAD_BUFFERS; i++)
        {
            if (slot_slice_[i] < 0)
                continue;
            const VkResult status = vkGetFenceStatus(ctx_->device(), shadow_upload_fences_[i]);
            if (status == VK_NOT_READY)
                continue;
            if (status != VK_SUCCESS)
            {
                fprintf(stderr, "Terrain upload failed: %s (%d)\n", vk_result_name(status), status);
                return SceneUpload::Failed;
            }
            finish_upload_slot(i);
            break;
        }

        if (next_slice_ < scene_slices_.size())
            return submit_upload_slice() ? SceneUpload::Pending : SceneUpload::Failed;
        for (int32_t slice : slot_slice_)
        {
            if (slice >= 0)
                return SceneUpload::Pending;
        }
        scene_slices_ = {};
        filled_chunks_ = {};
        loading_volume_ = nullptr;
        ready_ = true;
        return SceneUpload::Done;
    }

    /* Safe on a partially created scene: every handle is checked */
    void TerrainVolume::destroy_scene()
    {
        wait_all_shadow_uploads();
        shadow_upload_index_ = 0;
        for (int32_t &slice : slot_slice_)
            slice = -1;

        ctx_->destroy_image(&shadow_volume_);
        ctx_->destroy_buffer(&voxel_data_);
        ctx_->destroy_buffer(&chunk_headers_);
        ctx_->destroy_buffer(&material_palette_);
        ctx_->destroy_buffer(&staging_voxels_);
        ctx_->destroy_buffer(&staging_headers_);
        for (SceneSlice &slice : scene_slices_)
            ctx_->destroy_buffer(&slice.staging);

        for (std::vector<uint8_t> &mip : shadow_mips_)
            mip = {};
        scene_slices_ = {};
        next_slice_ = 0;
        filled_chunks_ = {};
        loading_volume_ = nullptr;

        params_ = {};
        ready_ = false;
    }

    bool TerrainVolume::update_shadow_volume(VoxelVolume *vol)
    {
        if (!ready_)
            return true;

        int32_t dirty_chunks[VOLUME_SHADOW_DIRTY_MAX];
        const int32_t dirty_count = volume_get_shadow_dirty_chunks(vol, dirty_chunks, VOLUME_SHADOW_DIRTY_MAX);
        if (dirty_count == 0)
            return true;

        bool occludes[VOXEL_MATERIAL_MAX];
        material_occlusion(occludes);

        uint32_t dims0[3];
        volume_shadow_mip_dims(vol, 0, dims0);
        ChunkBox boxes[VOLUME_SHADOW_DIRTY_MAX];

        PROFILE_BEGIN(PROFILE_SHADOW_TERRAIN_PACK);
        for (int32_t i = 0; i < dirty_count; i++)
            volume_pack_shadow_chunk(vol, occludes, dirty_chunks[i], shadow_mips_[0].data(), dims0[0], dims0[1], dims0[2]);
        const int32_t box_count = volume_shadow_chunk_boxes(vol, dirty_chunks, dirty_count, boxes);
        PROFILE_END(PROFILE_SHADOW_TERRAIN_PACK);

        /* Mip texels lie within one chunk: the boxes' mips depend on nothing else */
        PROFILE_BEGIN(PROFILE_SHADOW_MIP_REGEN);
        uint8_t *mips[VOLUME_SHADOW_MIPS];
        for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
            mips[level] = shadow_mips_[level].data();
        for (int32_t i = 0; i < box_count; i++)
            volume_shadow_build_mips(vol, &boxes[i], mips);
        PROFILE_END(PROFILE_SHADOW_MIP_REGEN);

        volume_clear_shadow_dirty(vol);

        PROFILE_BEGIN(PROFILE_SHADOW_UPLOAD);
        const bool uploaded = upload_shadow_boxes(vol, mips, boxes, box_count);
        PROFILE_END(PROFILE_SHADOW_UPLOAD);
        return uploaded;
    }

    int32_t TerrainVolume::upload_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_indices,
                                               uint32_t slot)
    {
        if (!vol || !ready_)
            return 0;

        PROFILE_BEGIN(PROFILE_VOXEL_UPLOAD);

        int32_t dirty_indices[VOLUME_MAX_UPLOADS_PER_FRAME];
        int32_t dirty_count = volume_get_dirty_chunks(vol, dirty_indices, VOLUME_MAX_UPLOADS_PER_FRAME);
        if (dirty_count > max_indices)
            dirty_count = max_indices;
        if (dirty_count <= 0)
        {
            PROFILE_END(PROFILE_VOXEL_UPLOAD);
            return 0;
        }

        /* This frame slot's region of the persistent staging buffers */
        const VkDeviceSize voxel_region = staging_voxels_frame_size_ * slot;
        const VkDeviceSize header_region = staging_headers_frame_size_ * slot;
        uint8_t *voxel_mapped = static_cast<uint8_t *>(staging_voxels_.mapped) + voxel_region;
        GPUChunkHeader *headers_mapped = reinterpret_cast<GPUChunkHeader *>(
            static_cast<uint8_t *>(staging_headers_.mapped) + header_region);

        VkBufferCopy voxel_copies[VOLUME_MAX_UPLOADS_PER_FRAME];
        VkBufferCopy header_copies[VOLUME_MAX_UPLOADS_PER_FRAME];
        uint32_t copy_count = 0;

        for (int32_t staging_idx = 0; staging_idx < dirty_count; staging_idx++)
        {
            const int32_t ci = dirty_indices[staging_idx];
            out_indices[staging_idx] = ci;
            if (ci < 0 || ci >= vol->total_chunks || vol->chunks[ci].state != CHUNK_STATE_DIRTY)
                continue;

            /* The dirty list can name a chunk twice in one frame (ring + bitmap recovery).
             * Two regions writing the same destination is invalid (VUID-vkCmdCopyBuffer-
             * pRegions-12482) and a write-after-write hazard, so copy each chunk once. */
            bool duplicate = false;
            for (int32_t prev = 0; prev < staging_idx; prev++)
                duplicate = duplicate || dirty_indices[prev] == ci;
            if (duplicate)
                continue;

            gpu_chunk_copy_voxels(&vol->chunks[ci], voxel_mapped + static_cast<size_t>(staging_idx) * GPU_CHUNK_DATA_SIZE);
            headers_mapped[staging_idx] = gpu_chunk_header_from_chunk(&vol->chunks[ci]);

            voxel_copies[copy_count].srcOffset = voxel_region + static_cast<VkDeviceSize>(staging_idx) * GPU_CHUNK_DATA_SIZE;
            voxel_copies[copy_count].dstOffset = static_cast<VkDeviceSize>(ci) * GPU_CHUNK_DATA_SIZE;
            voxel_copies[copy_count].size = GPU_CHUNK_DATA_SIZE;
            header_copies[copy_count].srcOffset = header_region + static_cast<VkDeviceSize>(staging_idx) * sizeof(GPUChunkHeader);
            header_copies[copy_count].dstOffset = static_cast<VkDeviceSize>(ci) * sizeof(GPUChunkHeader);
            header_copies[copy_count].size = sizeof(GPUChunkHeader);
            copy_count++;
        }

        if (copy_count > 0)
        {
            VkCommandBuffer cmd = upload_cmd_[slot];
            begin_one_time(cmd);

            /*
             * The copies overwrite chunk data that the raymarch of the previous, possibly still
             * executing frame reads (WAR), and this frame's raymarch reads the result (RAW). The
             * upload is submitted before the frame's command buffer on the same queue, so
             * pipeline barriers here order it against both.
             */
            BarrierBatch barriers(cmd);
            barriers.buffer(voxel_data_.buffer, CHUNK_READ, COPY_WRITE);
            barriers.buffer(chunk_headers_.buffer, CHUNK_READ, COPY_WRITE);
            barriers.record();

            vkCmdCopyBuffer(cmd, staging_voxels_.buffer, voxel_data_.buffer, copy_count, voxel_copies);
            vkCmdCopyBuffer(cmd, staging_headers_.buffer, chunk_headers_.buffer, copy_count, header_copies);

            barriers.buffer(voxel_data_.buffer, COPY_WRITE, CHUNK_READ);
            barriers.buffer(chunk_headers_.buffer, COPY_WRITE, CHUNK_READ);
            barriers.record();

            /* No fence: the frame's own fence covers this earlier submission */
            VkResult result = vkEndCommandBuffer(cmd);
            if (result == VK_SUCCESS)
            {
                VkSubmitInfo submit_info{};
                submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit_info.commandBufferCount = 1;
                submit_info.pCommandBuffers = &cmd;
                result = vkQueueSubmit(ctx_->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE);
            }
            if (result != VK_SUCCESS)
            {
                fprintf(stderr, "Chunk upload failed: %s (%d)\n", vk_result_name(result), result);
                PROFILE_END(PROFILE_VOXEL_UPLOAD);
                return -1;
            }
        }

        PROFILE_END(PROFILE_VOXEL_UPLOAD);
        return dirty_count;
    }

    void TerrainVolume::wait_for_shadow_upload(uint32_t index)
    {
        if (!shadow_upload_pending_[index])
            return;

        vkWaitForFences(ctx_->device(), 1, &shadow_upload_fences_[index], VK_TRUE, UINT64_MAX);
        vkResetFences(ctx_->device(), 1, &shadow_upload_fences_[index]);
        shadow_upload_pending_[index] = false;
    }

    void TerrainVolume::wait_all_shadow_uploads()
    {
        for (uint32_t i = 0; i < SHADOW_UPLOAD_BUFFERS; i++)
            wait_for_shadow_upload(i);
    }

    /* The regions' copies into the shadow volume, after the earlier frames' traces finished
     * reading it; every texel outside them stays */
    void TerrainVolume::record_shadow_copy(VkCommandBuffer cmd, VkBuffer staging, uint32_t region_count)
    {
        for (uint32_t i = 0; i < region_count; i++)
        {
            const ShadowCopyRegion &region = shadow_regions_[i];
            VkBufferImageCopy &copy = shadow_copies_[i];
            copy = {};
            copy.bufferOffset = region.staging_offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, region.level, 0, 1};
            copy.imageOffset = {static_cast<int32_t>(region.offset[0]), static_cast<int32_t>(region.offset[1]),
                                static_cast<int32_t>(region.offset[2])};
            copy.imageExtent = {region.extent[0], region.extent[1], region.extent[2]};
        }

        BarrierBatch barriers(cmd);
        barriers.transition(shadow_volume_, image_use::copy_dst);
        barriers.record();

        vkCmdCopyBufferToImage(cmd, staging, shadow_volume_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region_count,
                               shadow_copies_.data());

        barriers.transition(shadow_volume_, image_use::sampled(READER_STAGES));
        barriers.record();
    }

    /*
     * Copies every mip of the boxes into the shadow volume. An edit's boxes go through the next
     * of the SHADOW_UPLOAD_BUFFERS staging buffers: only that buffer's previous upload (two ago)
     * has to be complete, so this rarely blocks, and the upload is submitted ahead of the frame's
     * command buffer.
     */
    bool TerrainVolume::upload_shadow_boxes(const VoxelVolume *vol, const uint8_t *const mips[VOLUME_SHADOW_MIPS],
                                            const ChunkBox *boxes, int32_t box_count)
    {
        const uint32_t region_count = static_cast<uint32_t>(box_count) * VOLUME_SHADOW_MIPS;
        volume_shadow_copy_regions(boxes, box_count, shadow_regions_.data());

        const uint32_t idx = shadow_upload_index_;
        shadow_upload_index_ = (shadow_upload_index_ + 1) % SHADOW_UPLOAD_BUFFERS;
        wait_for_shadow_upload(idx);
        volume_shadow_stage_regions(vol, mips, shadow_regions_.data(), static_cast<int32_t>(region_count),
                                    static_cast<uint8_t *>(shadow_staging_buffers_[idx].mapped));

        VkCommandBuffer cmd = shadow_upload_cmds_[idx];
        begin_one_time(cmd);
        record_shadow_copy(cmd, shadow_staging_buffers_[idx].buffer, region_count);
        VkResult result = vkEndCommandBuffer(cmd);
        if (result == VK_SUCCESS)
        {
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            result = vkQueueSubmit(ctx_->graphics_queue(), 1, &submit_info, shadow_upload_fences_[idx]);
        }
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Shadow volume upload failed: %s (%d)\n", vk_result_name(result), result);
            return false;
        }
        shadow_upload_pending_[idx] = true;
        return true;
    }

}
