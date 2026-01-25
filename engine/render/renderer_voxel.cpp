#include "renderer.h"
#include "gpu_volume.h"
#include "voxel_push_constants.h"
#include "engine/core/profile.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/unified_volume.h"
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

namespace patch
{

    bool Renderer::create_voxel_descriptor_layout()
    {
        VkDescriptorSetLayoutBinding bindings[5]{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 5;
        layout_info.pBindings = bindings;

        return vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &voxel_descriptor_layout_) == VK_SUCCESS;
    }

    bool Renderer::create_voxel_descriptors(int32_t total_chunks)
    {
        if (voxel_descriptor_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device_, voxel_descriptor_pool_, nullptr);
            voxel_descriptor_pool_ = VK_NULL_HANDLE;
        }

        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[1].descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT; /* material palette + temporal UBO */
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT; /* depth texture */

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &voxel_descriptor_pool_) != VK_SUCCESS)
        {
            return false;
        }

        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            layouts[i] = voxel_descriptor_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = voxel_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        alloc_info.pSetLayouts = layouts;

        if (vkAllocateDescriptorSets(device_, &alloc_info, voxel_descriptor_sets_) != VK_SUCCESS)
        {
            return false;
        }

        VkDeviceSize voxel_data_size = static_cast<VkDeviceSize>(total_chunks) * GPU_CHUNK_DATA_SIZE;
        VkDeviceSize headers_size = static_cast<VkDeviceSize>(total_chunks) * sizeof(GPUChunkHeader);
        VkDeviceSize palette_size = sizeof(GPUMaterialPalette);

        create_buffer(voxel_data_size,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &voxel_data_buffer_);

        create_buffer(headers_size,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &voxel_headers_buffer_);

        create_buffer(palette_size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &voxel_material_buffer_);

        VkDeviceSize temporal_ubo_size = sizeof(VoxelTemporalUBO);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            create_buffer(temporal_ubo_size,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &voxel_temporal_ubo_[i]);
            voxel_temporal_ubo_mapped_[i] = gpu_allocator_.map(voxel_temporal_ubo_[i].allocation);
        }

        /* Create persistent staging buffers for chunk uploads (avoids per-frame allocation) */
        VkDeviceSize staging_voxel_size = static_cast<VkDeviceSize>(VOLUME_MAX_DIRTY_PER_FRAME) * GPU_CHUNK_DATA_SIZE;
        VkDeviceSize staging_header_size = static_cast<VkDeviceSize>(VOLUME_MAX_DIRTY_PER_FRAME) * sizeof(GPUChunkHeader);

        create_buffer(staging_voxel_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_voxels_buffer_);

        create_buffer(staging_header_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_headers_buffer_);

        /* Persistently map staging buffers */
        staging_voxels_mapped_ = gpu_allocator_.map(staging_voxels_buffer_.allocation);
        staging_headers_mapped_ = gpu_allocator_.map(staging_headers_buffer_.allocation);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo voxel_buffer_info{};
            voxel_buffer_info.buffer = voxel_data_buffer_.buffer;
            voxel_buffer_info.offset = 0;
            voxel_buffer_info.range = voxel_data_size;

            VkDescriptorBufferInfo headers_buffer_info{};
            headers_buffer_info.buffer = voxel_headers_buffer_.buffer;
            headers_buffer_info.offset = 0;
            headers_buffer_info.range = headers_size;

            VkDescriptorBufferInfo palette_buffer_info{};
            palette_buffer_info.buffer = voxel_material_buffer_.buffer;
            palette_buffer_info.offset = 0;
            palette_buffer_info.range = palette_size;

            VkDescriptorBufferInfo temporal_buffer_info{};
            temporal_buffer_info.buffer = voxel_temporal_ubo_[i].buffer;
            temporal_buffer_info.offset = 0;
            temporal_buffer_info.range = temporal_ubo_size;

            VkDescriptorImageInfo depth_image_info{};
            depth_image_info.sampler = depth_sampler_;
            depth_image_info.imageView = depth_image_view_;
            depth_image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[5]{};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = voxel_descriptor_sets_[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &voxel_buffer_info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = voxel_descriptor_sets_[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &headers_buffer_info;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = voxel_descriptor_sets_[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &palette_buffer_info;

            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = voxel_descriptor_sets_[i];
            writes[3].dstBinding = 3;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &temporal_buffer_info;

            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = voxel_descriptor_sets_[i];
            writes[4].dstBinding = 4;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[4].descriptorCount = 1;
            writes[4].pImageInfo = &depth_image_info;

            vkUpdateDescriptorSets(device_, 5, writes, 0, nullptr);
        }

        voxel_total_chunks_ = total_chunks;
        return true;
    }

    void Renderer::update_voxel_depth_descriptor()
    {
        if (!voxel_resources_initialized_ || depth_image_view_ == VK_NULL_HANDLE)
            return;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo depth_image_info{};
            depth_image_info.sampler = depth_sampler_;
            depth_image_info.imageView = depth_image_view_;
            depth_image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = voxel_descriptor_sets_[i];
            write.dstBinding = 4;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &depth_image_info;

            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    void Renderer::init_volume_for_raymarching(const VoxelVolume *vol)
    {
        PROFILE_BEGIN(PROFILE_VOLUME_INIT);

        if (!vol)
        {
            PROFILE_END(PROFILE_VOLUME_INIT);
            return;
        }

        /* Reset scene-dependent state for clean temporal accumulation */
        reset_scene_state();

        if (!voxel_resources_initialized_ || voxel_total_chunks_ != vol->total_chunks)
        {
            if (voxel_resources_initialized_)
            {
                vkDeviceWaitIdle(device_);
                destroy_buffer(&voxel_data_buffer_);
                destroy_buffer(&voxel_headers_buffer_);
                destroy_buffer(&voxel_material_buffer_);
                for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                {
                    if (voxel_temporal_ubo_mapped_[i])
                    {
                        gpu_allocator_.unmap(voxel_temporal_ubo_[i].allocation);
                        voxel_temporal_ubo_mapped_[i] = nullptr;
                    }
                    destroy_buffer(&voxel_temporal_ubo_[i]);
                }
            }

            if (!create_voxel_descriptors(vol->total_chunks))
            {
                return;
            }

            voxel_resources_initialized_ = true;

            /* Initialize compute raymarching pipelines first (creates shadow_output_view_) */
            if (!compute_resources_initialized_)
            {
                if (!init_compute_raymarching())
                {
                    fprintf(stderr, "Warning: Compute raymarching init failed, using fragment path\n");
                }
                else
                {
                    if (!create_gbuffer_compute_descriptor_sets())
                    {
                        fprintf(stderr, "Warning: G-buffer compute descriptors failed\n");
                    }
                    if (!create_shadow_compute_descriptor_sets())
                    {
                        fprintf(stderr, "Warning: Shadow compute descriptors failed\n");
                    }
                    if (!create_ao_compute_descriptor_sets())
                    {
                        fprintf(stderr, "Warning: AO compute descriptors failed\n");
                    }
                    if (!create_temporal_ao_descriptor_sets())
                    {
                        fprintf(stderr, "Warning: Temporal AO descriptors failed\n");
                    }
                }
            }

            /* Create deferred descriptors after compute (needs shadow_output_view_) */
            if (!init_deferred_descriptors())
            {
                return;
            }
        }

        GPUMaterialPalette palette{};
        for (int32_t i = 0; i < material_count_ && i < GPU_MATERIAL_PALETTE_SIZE; i++)
        {
            if (use_full_materials_)
            {
                palette.colors[i].r = material_entries_[i].r;
                palette.colors[i].g = material_entries_[i].g;
                palette.colors[i].b = material_entries_[i].b;
                palette.colors[i].emissive = material_entries_[i].emissive;
                palette.colors[i].roughness = material_entries_[i].roughness;
                palette.colors[i].metallic = material_entries_[i].metallic;
                palette.colors[i].flags = material_entries_[i].flags;
                palette.colors[i].pad = 0.0f;
            }
            else
            {
                palette.colors[i].r = material_palette_[i].x;
                palette.colors[i].g = material_palette_[i].y;
                palette.colors[i].b = material_palette_[i].z;
                palette.colors[i].emissive = 0.0f;
                palette.colors[i].roughness = 0.5f;
                palette.colors[i].metallic = 0.0f;
                palette.colors[i].flags = 0.0f;
                palette.colors[i].pad = 0.0f;
            }
        }

        int32_t upload_count = material_count_ > 0 ? material_count_ : 1;
        VkDeviceSize palette_upload_size = static_cast<VkDeviceSize>(upload_count) * sizeof(GPUMaterialColor);
        void *mapped = gpu_allocator_.map(voxel_material_buffer_.allocation);
        memcpy(mapped, &palette, palette_upload_size);
        gpu_allocator_.unmap(voxel_material_buffer_.allocation);

        VkDeviceSize voxel_data_size = static_cast<VkDeviceSize>(vol->total_chunks) * GPU_CHUNK_DATA_SIZE;
        VkDeviceSize headers_size = static_cast<VkDeviceSize>(vol->total_chunks) * sizeof(GPUChunkHeader);

        VulkanBuffer staging_voxels{};
        create_buffer(voxel_data_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_voxels);

        VulkanBuffer staging_headers{};
        create_buffer(headers_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_headers);

        uint8_t *voxel_mapped = static_cast<uint8_t *>(gpu_allocator_.map(staging_voxels.allocation));
        int32_t total_solid = 0;
        for (int32_t ci = 0; ci < vol->total_chunks; ci++)
        {
            gpu_chunk_copy_voxels(&vol->chunks[ci], voxel_mapped + ci * GPU_CHUNK_DATA_SIZE);
        }
        /* DEBUG: Check chunk 0 data */
        for (int32_t i = 0; i < 160; i++) /* First 5 Y layers: 32*5 = 160 voxels */
        {
            if (voxel_mapped[i] != 0)
                total_solid++;
        }
        printf("  DEBUG: Chunk 0 first 160 voxels have %d solid\n", total_solid);
        printf("  DEBUG: First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
               voxel_mapped[0], voxel_mapped[1], voxel_mapped[2], voxel_mapped[3],
               voxel_mapped[4], voxel_mapped[5], voxel_mapped[6], voxel_mapped[7]);
        gpu_allocator_.unmap(staging_voxels.allocation);

        GPUChunkHeader *headers_mapped = static_cast<GPUChunkHeader *>(gpu_allocator_.map(staging_headers.allocation));
        for (int32_t ci = 0; ci < vol->total_chunks; ci++)
        {
            headers_mapped[ci] = gpu_chunk_header_from_chunk(&vol->chunks[ci]);
        }
        /* DEBUG: Show chunk 0 header */
        printf("  DEBUG: Chunk 0 header: level0_lo=%08x level0_hi=%08x packed=%08x\n",
               headers_mapped[0].level0_lo, headers_mapped[0].level0_hi, headers_mapped[0].packed);
        printf("  DEBUG: Chunk 0 has_any=%d solid_count=%d\n",
               (headers_mapped[0].packed & 0xFF), (headers_mapped[0].packed >> 16));
        gpu_allocator_.unmap(staging_headers.allocation);

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = command_pool_;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkBufferCopy copy_region{};
        copy_region.size = voxel_data_size;
        vkCmdCopyBuffer(cmd, staging_voxels.buffer, voxel_data_buffer_.buffer, 1, &copy_region);

        copy_region.size = headers_size;
        vkCmdCopyBuffer(cmd, staging_headers.buffer, voxel_headers_buffer_.buffer, 1, &copy_region);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue_);

        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        destroy_buffer(&staging_voxels);
        destroy_buffer(&staging_headers);

        int32_t voxels_x = vol->chunks_x * CHUNK_SIZE;
        int32_t voxels_y = vol->chunks_y * CHUNK_SIZE;
        int32_t voxels_z = vol->chunks_z * CHUNK_SIZE;

        uint32_t w0 = static_cast<uint32_t>(voxels_x >> 1);
        uint32_t h0 = static_cast<uint32_t>(voxels_y >> 1);
        uint32_t d0 = static_cast<uint32_t>(voxels_z >> 1);

        bool needs_shadow_update = !shadow_volume_image_ ||
                                   shadow_volume_dims_[0] != w0 ||
                                   shadow_volume_dims_[1] != h0 ||
                                   shadow_volume_dims_[2] != d0;

        if (w0 > 0 && h0 > 0 && d0 > 0 && gbuffer_initialized_ && needs_shadow_update)
        {
            size_t size0 = static_cast<size_t>(w0) * h0 * d0;
            uint32_t w1 = w0 >> 1;
            if (w1 == 0)
                w1 = 1;
            uint32_t h1 = h0 >> 1;
            if (h1 == 0)
                h1 = 1;
            uint32_t d1 = d0 >> 1;
            if (d1 == 0)
                d1 = 1;
            size_t size1 = static_cast<size_t>(w1) * h1 * d1;

            uint32_t w2 = w1 >> 1;
            if (w2 == 0)
                w2 = 1;
            uint32_t h2 = h1 >> 1;
            if (h2 == 0)
                h2 = 1;
            uint32_t d2 = d1 >> 1;
            if (d2 == 0)
                d2 = 1;
            size_t size2 = static_cast<size_t>(w2) * h2 * d2;

            std::vector<uint8_t> mip0(size0);
            std::vector<uint8_t> mip1(size1);
            std::vector<uint8_t> mip2(size2);

            uint32_t out_w, out_h, out_d;
            volume_pack_shadow_volume(vol, mip0.data(), &out_w, &out_h, &out_d);
            volume_generate_shadow_mips(mip0.data(), w0, h0, d0, mip1.data(), mip2.data());

            if (!shadow_volume_image_ || shadow_volume_dims_[0] != w0 ||
                shadow_volume_dims_[1] != h0 || shadow_volume_dims_[2] != d0)
            {
                destroy_shadow_volume_resources();
                create_shadow_volume_resources(w0, h0, d0);
                update_shadow_volume_descriptor();
                update_ao_volume_descriptor();
            }

            upload_shadow_volume(mip0.data(), w0, h0, d0,
                                 mip1.data(), w1, h1, d1,
                                 mip2.data(), w2, h2, d2);

            /* Wait for initial upload to complete before first frame renders */
            cleanup_all_shadow_uploads();

            shadow_volume_last_frame_ = vol->current_frame;
        }

        PROFILE_END(PROFILE_VOLUME_INIT);
    }

    void Renderer::update_shadow_volume(VoxelVolume *vol,
                                        const VoxelObjectWorld *objects,
                                        const ParticleSystem *particles)
    {
        if (!vol || !gbuffer_initialized_ || !shadow_volume_image_)
            return;

        int32_t voxels_x = vol->chunks_x * CHUNK_SIZE;
        int32_t voxels_y = vol->chunks_y * CHUNK_SIZE;
        int32_t voxels_z = vol->chunks_z * CHUNK_SIZE;

        uint32_t w0 = static_cast<uint32_t>(voxels_x >> 1);
        uint32_t h0 = static_cast<uint32_t>(voxels_y >> 1);
        uint32_t d0 = static_cast<uint32_t>(voxels_z >> 1);

        if (w0 == 0 || h0 == 0 || d0 == 0)
            return;

        uint32_t w1 = w0 >> 1;
        if (w1 == 0)
            w1 = 1;
        uint32_t h1 = h0 >> 1;
        if (h1 == 0)
            h1 = 1;
        uint32_t d1 = d0 >> 1;
        if (d1 == 0)
            d1 = 1;

        uint32_t w2 = w1 >> 1;
        if (w2 == 0)
            w2 = 1;
        uint32_t h2 = h1 >> 1;
        if (h2 == 0)
            h2 = 1;
        uint32_t d2 = d1 >> 1;
        if (d2 == 0)
            d2 = 1;

        size_t size0 = static_cast<size_t>(w0) * h0 * d0;
        size_t size1 = static_cast<size_t>(w1) * h1 * d1;
        size_t size2 = static_cast<size_t>(w2) * h2 * d2;

        bool objects_present = (objects && objects->object_count > 0);
        int32_t active_particle_count = 0;
        bool particles_active = false;

        int32_t dirty_chunks[VOLUME_SHADOW_DIRTY_MAX];
        int32_t dirty_count = volume_get_shadow_dirty_chunks(vol, dirty_chunks, VOLUME_SHADOW_DIRTY_MAX);

        /* Check if volume dimensions changed (requires full rebuild) */
        bool volume_resized = !shadow_volume_initialized_ || shadow_mip0_.size() != size0;

        /* Check if terrain needs update (dirty chunks or full rebuild flag) */
        bool terrain_dirty = volume_shadow_needs_full_rebuild(vol) || dirty_count > 0;

        /* Detect if any objects moved (we re-stamp ALL objects if any moved) */
        bool any_object_moved = false;
        bool new_objects_added = false;

        if (objects_present)
        {
            int32_t obj_count = objects->object_count;
            if (obj_count > static_cast<int32_t>(MAX_SHADOW_OBJECTS))
                obj_count = static_cast<int32_t>(MAX_SHADOW_OBJECTS);

            /* Detect new objects */
            if (obj_count > shadow_object_count_)
            {
                new_objects_added = true;
            }

            /* Check each object for movement - stop at first moved object since we re-stamp all anyway */
            for (int32_t i = 0; i < obj_count && !any_object_moved; i++)
            {
                const VoxelObject *obj = &objects->objects[i];
                if (!obj->active)
                    continue;

                ShadowObjectState *state = &shadow_object_states_[i];

                if (!state->valid)
                {
                    /* New object, needs stamp */
                    any_object_moved = true;
                }
                else
                {
                    /* Check position delta */
                    float dx = obj->position.x - state->position.x;
                    float dy = obj->position.y - state->position.y;
                    float dz = obj->position.z - state->position.z;
                    float dist_sq = dx * dx + dy * dy + dz * dz;

                    /* Check orientation delta (dot product for quaternion difference) */
                    float dot = obj->orientation.x * state->orientation.x +
                                obj->orientation.y * state->orientation.y +
                                obj->orientation.z * state->orientation.z +
                                obj->orientation.w * state->orientation.w;
                    float orient_diff = 1.0f - dot * dot;

                    if (dist_sq > SHADOW_POSITION_THRESHOLD * SHADOW_POSITION_THRESHOLD ||
                        orient_diff > 0.0001f)
                    {
                        any_object_moved = true;
                    }
                }
            }
        }

        int32_t particle_min[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
        int32_t particle_max[3] = {INT32_MIN, INT32_MIN, INT32_MIN};

        if (particles && particles->count > 0)
        {
            for (int32_t i = 0; i < particles->count; i++)
            {
                const Particle *p = &particles->particles[i];
                if (!p->active)
                    continue;

                float interp_x = p->prev_position.x + interp_alpha_ * (p->position.x - p->prev_position.x);
                float interp_y = p->prev_position.y + interp_alpha_ * (p->position.y - p->prev_position.y);
                float interp_z = p->prev_position.z + interp_alpha_ * (p->position.z - p->prev_position.z);

                float rel_x = interp_x - vol->bounds.min_x;
                float rel_y = interp_y - vol->bounds.min_y;
                float rel_z = interp_z - vol->bounds.min_z;

                int32_t min_vx = (int32_t)((rel_x - p->radius) / vol->voxel_size);
                int32_t min_vy = (int32_t)((rel_y - p->radius) / vol->voxel_size);
                int32_t min_vz = (int32_t)((rel_z - p->radius) / vol->voxel_size);
                int32_t max_vx = (int32_t)((rel_x + p->radius) / vol->voxel_size);
                int32_t max_vy = (int32_t)((rel_y + p->radius) / vol->voxel_size);
                int32_t max_vz = (int32_t)((rel_z + p->radius) / vol->voxel_size);

                if (min_vx < 0)
                    min_vx = 0;
                if (min_vy < 0)
                    min_vy = 0;
                if (min_vz < 0)
                    min_vz = 0;
                if (max_vx >= voxels_x)
                    max_vx = voxels_x - 1;
                if (max_vy >= voxels_y)
                    max_vy = voxels_y - 1;
                if (max_vz >= voxels_z)
                    max_vz = voxels_z - 1;

                if (min_vx > max_vx || min_vy > max_vy || min_vz > max_vz)
                    continue;

                if (min_vx < particle_min[0])
                    particle_min[0] = min_vx;
                if (min_vy < particle_min[1])
                    particle_min[1] = min_vy;
                if (min_vz < particle_min[2])
                    particle_min[2] = min_vz;
                if (max_vx > particle_max[0])
                    particle_max[0] = max_vx;
                if (max_vy > particle_max[1])
                    particle_max[1] = max_vy;
                if (max_vz > particle_max[2])
                    particle_max[2] = max_vz;

                active_particle_count++;
            }
        }

        if (active_particle_count > 0)
            particles_active = true;

        bool particle_region_valid = false;
        int32_t particle_region_min[3] = {0, 0, 0};
        int32_t particle_region_max[3] = {0, 0, 0};

        if (particles_active)
        {
            particle_region_min[0] = particle_min[0];
            particle_region_min[1] = particle_min[1];
            particle_region_min[2] = particle_min[2];
            particle_region_max[0] = particle_max[0];
            particle_region_max[1] = particle_max[1];
            particle_region_max[2] = particle_max[2];
            particle_region_valid = true;
        }

        if (shadow_particle_aabb_valid_)
        {
            if (!particle_region_valid)
            {
                particle_region_min[0] = shadow_particle_aabb_min_[0];
                particle_region_min[1] = shadow_particle_aabb_min_[1];
                particle_region_min[2] = shadow_particle_aabb_min_[2];
                particle_region_max[0] = shadow_particle_aabb_max_[0];
                particle_region_max[1] = shadow_particle_aabb_max_[1];
                particle_region_max[2] = shadow_particle_aabb_max_[2];
                particle_region_valid = true;
            }
            else
            {
                if (shadow_particle_aabb_min_[0] < particle_region_min[0])
                    particle_region_min[0] = shadow_particle_aabb_min_[0];
                if (shadow_particle_aabb_min_[1] < particle_region_min[1])
                    particle_region_min[1] = shadow_particle_aabb_min_[1];
                if (shadow_particle_aabb_min_[2] < particle_region_min[2])
                    particle_region_min[2] = shadow_particle_aabb_min_[2];
                if (shadow_particle_aabb_max_[0] > particle_region_max[0])
                    particle_region_max[0] = shadow_particle_aabb_max_[0];
                if (shadow_particle_aabb_max_[1] > particle_region_max[1])
                    particle_region_max[1] = shadow_particle_aabb_max_[1];
                if (shadow_particle_aabb_max_[2] > particle_region_max[2])
                    particle_region_max[2] = shadow_particle_aabb_max_[2];
            }
        }

        /* Determine rebuild strategy
         * volume_resized = buffers changed size, must do full rebuild
         * shadow_needs_full_rebuild = dirty chunk array overflowed, can use bitmap iteration */
        bool bitmap_overflow = volume_shadow_needs_full_rebuild(vol) && !volume_resized;
        bool needs_full_rebuild = volume_resized;

        /* When bitmap overflowed, re-fetch dirty chunks (now scans bitmap) */
        if (bitmap_overflow)
        {
            dirty_count = volume_get_shadow_dirty_chunks(vol, dirty_chunks, VOLUME_SHADOW_DIRTY_MAX);
            terrain_dirty = dirty_count > 0;
        }

        bool needs_terrain_repack = needs_full_rebuild || terrain_dirty;
        bool needs_object_stamp = needs_full_rebuild || any_object_moved || new_objects_added;
        bool needs_particle_update = particles_active || shadow_particle_aabb_valid_;

        if (!needs_terrain_repack && !needs_object_stamp && !needs_particle_update)
        {
            return; /* Nothing changed, skip update */
        }

        /* Resize buffers if needed */
        if (volume_resized)
        {
            shadow_mip0_.resize(size0);
            shadow_mip1_.resize(size1);
            shadow_mip2_.resize(size2);

            shadow_mip_dims_[0][0] = w0;
            shadow_mip_dims_[0][1] = h0;
            shadow_mip_dims_[0][2] = d0;
            shadow_mip_dims_[1][0] = w1;
            shadow_mip_dims_[1][1] = h1;
            shadow_mip_dims_[1][2] = d1;
            shadow_mip_dims_[2][0] = w2;
            shadow_mip_dims_[2][1] = h2;
            shadow_mip_dims_[2][2] = d2;
        }

        /* Terrain update: either full repack or incremental chunk updates */
        PROFILE_BEGIN(PROFILE_SHADOW_TERRAIN_PACK);
        if (needs_full_rebuild)
        {
            uint32_t out_w, out_h, out_d;
            volume_pack_shadow_volume(vol, shadow_mip0_.data(), &out_w, &out_h, &out_d);
        }
        else if (terrain_dirty && dirty_count > 0)
        {
            for (int32_t i = 0; i < dirty_count; i++)
            {
                int32_t chunk_idx = dirty_chunks[i];
                volume_pack_shadow_chunk(vol, chunk_idx, shadow_mip0_.data(), w0, h0, d0);
            }
        }
        PROFILE_END(PROFILE_SHADOW_TERRAIN_PACK);

        /* NOTE: Object shadow stamping removed - objects now traced directly in shadow shader */
        (void)any_object_moved;
        (void)new_objects_added;

        /* Particle shadows: now traced via G-buffer surface data, no separate stamping needed */
        (void)particles_active;

        /* Mip generation: separate for terrain and object volumes */
        PROFILE_BEGIN(PROFILE_SHADOW_MIP_REGEN);

        /* Terrain mip generation */
        bool needs_terrain_mip_update = needs_terrain_repack;
        if (needs_terrain_mip_update)
        {
            if (needs_full_rebuild)
            {
                /* Volume resized - must do full mip regeneration for terrain */
                volume_generate_shadow_mips(shadow_mip0_.data(), w0, h0, d0,
                                            shadow_mip1_.data(), shadow_mip2_.data());
            }
            else if (dirty_count > 0)
            {
                /* Region-based mip update for dirty terrain chunks */
                int32_t min_cx = INT32_MAX, min_cy = INT32_MAX, min_cz = INT32_MAX;
                int32_t max_cx = INT32_MIN, max_cy = INT32_MIN, max_cz = INT32_MIN;

                for (int32_t i = 0; i < dirty_count; i++)
                {
                    int32_t chunk_idx = dirty_chunks[i];
                    int32_t cx = chunk_idx % vol->chunks_x;
                    int32_t cy = (chunk_idx / vol->chunks_x) % vol->chunks_y;
                    int32_t cz = chunk_idx / (vol->chunks_x * vol->chunks_y);

                    if (cx < min_cx)
                        min_cx = cx;
                    if (cy < min_cy)
                        min_cy = cy;
                    if (cz < min_cz)
                        min_cz = cz;
                    if (cx > max_cx)
                        max_cx = cx;
                    if (cy > max_cy)
                        max_cy = cy;
                    if (cz > max_cz)
                        max_cz = cz;
                }

                /* Convert chunk bounds to mip0 coordinates (shadow mip0 is half-res) */
                int32_t min_vx = min_cx * CHUNK_SIZE;
                int32_t min_vy = min_cy * CHUNK_SIZE;
                int32_t min_vz = min_cz * CHUNK_SIZE;
                int32_t max_vx = (max_cx + 1) * CHUNK_SIZE - 1;
                int32_t max_vy = (max_cy + 1) * CHUNK_SIZE - 1;
                int32_t max_vz = (max_cz + 1) * CHUNK_SIZE - 1;

                volume_generate_shadow_mips_for_region(
                    min_vx >> 1, min_vy >> 1, min_vz >> 1,
                    max_vx >> 1, max_vy >> 1, max_vz >> 1,
                    shadow_mip0_.data(), w0, h0, d0,
                    shadow_mip1_.data(), w1, h1, d1,
                    shadow_mip2_.data(), w2, h2, d2);
            }
        }

        PROFILE_END(PROFILE_SHADOW_MIP_REGEN);

        shadow_volume_initialized_ = true;
        shadow_particle_count_ = active_particle_count;

        volume_clear_shadow_dirty(vol);

        PROFILE_BEGIN(PROFILE_SHADOW_UPLOAD);
        /* Upload terrain shadow volume */
        if (needs_terrain_mip_update || needs_full_rebuild)
        {
            upload_shadow_volume(shadow_mip0_.data(), w0, h0, d0,
                                 shadow_mip1_.data(), w1, h1, d1,
                                 shadow_mip2_.data(), w2, h2, d2);
        }
        PROFILE_END(PROFILE_SHADOW_UPLOAD);

        /* Update shadow compute descriptors with new shadow volume textures */
        update_shadow_volume_descriptor();
        update_ao_volume_descriptor();
    }

    int32_t Renderer::upload_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_indices)
    {
        PROFILE_BEGIN(PROFILE_CHUNK_UPLOAD);

        if (!vol || !voxel_resources_initialized_)
        {
            PROFILE_END(PROFILE_CHUNK_UPLOAD);
            return 0;
        }

        if (pending_destroy_count_ > 0)
        {
            uint64_t completed_value = 0;
            vkGetSemaphoreCounterValue(device_, upload_timeline_semaphore_, &completed_value);

            uint32_t write_idx = 0;
            for (uint32_t i = 0; i < pending_destroy_count_; i++)
            {
                if (pending_destroys_[i].timeline_value <= completed_value)
                {
                    destroy_buffer(&pending_destroys_[i].buffer);
                }
                else
                {
                    pending_destroys_[write_idx++] = pending_destroys_[i];
                }
            }
            pending_destroy_count_ = write_idx;
        }

        int32_t dirty_indices[VOLUME_MAX_UPLOADS_PER_FRAME];
        int32_t dirty_count = volume_get_dirty_chunks(vol, dirty_indices, VOLUME_MAX_UPLOADS_PER_FRAME);

        if (dirty_count <= 0)
        {
            PROFILE_END(PROFILE_CHUNK_UPLOAD);
            return 0;
        }

        if (pending_destroy_count_ + 2 > MAX_PENDING_DESTROYS)
        {
            VkSemaphoreWaitInfo wait_info{};
            wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.semaphoreCount = 1;
            wait_info.pSemaphores = &upload_timeline_semaphore_;
            uint64_t wait_value = pending_destroys_[0].timeline_value;
            wait_info.pValues = &wait_value;
            vkWaitSemaphores(device_, &wait_info, UINT64_MAX);

            uint64_t completed_value = 0;
            vkGetSemaphoreCounterValue(device_, upload_timeline_semaphore_, &completed_value);
            uint32_t write_idx = 0;
            for (uint32_t i = 0; i < pending_destroy_count_; i++)
            {
                if (pending_destroys_[i].timeline_value <= completed_value)
                {
                    destroy_buffer(&pending_destroys_[i].buffer);
                }
                else
                {
                    pending_destroys_[write_idx++] = pending_destroys_[i];
                }
            }
            pending_destroy_count_ = write_idx;
        }

        /* Use persistent staging buffers (already mapped at init) */
        uint8_t *voxel_mapped = static_cast<uint8_t *>(staging_voxels_mapped_);
        GPUChunkHeader *headers_mapped = static_cast<GPUChunkHeader *>(staging_headers_mapped_);

        std::vector<VkBufferCopy> voxel_copies;
        std::vector<VkBufferCopy> header_copies;
        voxel_copies.reserve(dirty_count);
        header_copies.reserve(dirty_count);

        for (int32_t staging_idx = 0; staging_idx < dirty_count; staging_idx++)
        {
            int32_t ci = dirty_indices[staging_idx];
            if (ci < 0 || ci >= vol->total_chunks)
                continue;
            if (vol->chunks[ci].state != CHUNK_STATE_DIRTY)
                continue;

            gpu_chunk_copy_voxels(&vol->chunks[ci], voxel_mapped + staging_idx * GPU_CHUNK_DATA_SIZE);
            headers_mapped[staging_idx] = gpu_chunk_header_from_chunk(&vol->chunks[ci]);

            VkBufferCopy voxel_copy{};
            voxel_copy.srcOffset = staging_idx * GPU_CHUNK_DATA_SIZE;
            voxel_copy.dstOffset = ci * GPU_CHUNK_DATA_SIZE;
            voxel_copy.size = GPU_CHUNK_DATA_SIZE;
            voxel_copies.push_back(voxel_copy);

            VkBufferCopy header_copy{};
            header_copy.srcOffset = staging_idx * sizeof(GPUChunkHeader);
            header_copy.dstOffset = ci * sizeof(GPUChunkHeader);
            header_copy.size = sizeof(GPUChunkHeader);
            header_copies.push_back(header_copy);
        }

        /* No unmap needed - persistent buffers stay mapped */

        VkCommandBuffer upload_cmd = upload_cmd_[current_frame_];
        vkResetCommandBuffer(upload_cmd, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(upload_cmd, &begin_info);

        if (!voxel_copies.empty())
        {
            vkCmdCopyBuffer(upload_cmd, staging_voxels_buffer_.buffer, voxel_data_buffer_.buffer,
                            static_cast<uint32_t>(voxel_copies.size()), voxel_copies.data());
        }

        if (!header_copies.empty())
        {
            vkCmdCopyBuffer(upload_cmd, staging_headers_buffer_.buffer, voxel_headers_buffer_.buffer,
                            static_cast<uint32_t>(header_copies.size()), header_copies.data());
        }

        vkEndCommandBuffer(upload_cmd);

        uint64_t signal_value = ++upload_timeline_value_;

        VkTimelineSemaphoreSubmitInfo timeline_submit{};
        timeline_submit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline_submit.signalSemaphoreValueCount = 1;
        timeline_submit.pSignalSemaphoreValues = &signal_value;

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = &timeline_submit;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &upload_cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &upload_timeline_semaphore_;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);

        /* No deferred destroy needed - persistent buffers are reused */

        int32_t copy_count = dirty_count;
        if (copy_count > max_indices)
            copy_count = max_indices;
        if (out_indices && copy_count > 0)
        {
            for (int32_t i = 0; i < copy_count; i++)
                out_indices[i] = dirty_indices[i];
        }

        PROFILE_END(PROFILE_CHUNK_UPLOAD);
        return dirty_count;
    }

    void Renderer::reset_scene_state()
    {
        /* Invalidate temporal history - prevents using stale data from previous scene */
        temporal_shadow_history_valid_ = false;
        temporal_ao_history_valid_ = false;
        taa_history_valid_ = false;

        /* Reset shadow volume CPU-side state to trigger full rebuild */
        shadow_volume_initialized_ = false;

        /* Reset shadow object tracking */
        for (uint32_t i = 0; i < MAX_SHADOW_OBJECTS; i++)
        {
            shadow_object_states_[i].valid = false;
        }
        shadow_stamp_cursor_ = 0;
        shadow_object_count_ = 0;
        shadow_particle_count_ = 0;
        shadow_needs_terrain_update_ = false;
        shadow_particle_aabb_valid_ = false;
        shadow_particle_aabb_min_[0] = 0;
        shadow_particle_aabb_min_[1] = 0;
        shadow_particle_aabb_min_[2] = 0;
        shadow_particle_aabb_max_[0] = 0;
        shadow_particle_aabb_max_[1] = 0;
        shadow_particle_aabb_max_[2] = 0;

        /* Reset voxel object tracking */
        vobj_last_world_ = nullptr;
        vobj_prev_object_count_ = 0;
        memset(vobj_dirty_mask_, 0, sizeof(vobj_dirty_mask_));
        memset(vobj_revision_cache_, 0, sizeof(vobj_revision_cache_));

        /* Reset camera interpolation state to avoid motion vector artifacts */
        camera_initialized_ = false;

        /* Sync previous frame matrices to current to prevent temporal reprojection artifacts */
        prev_view_matrix_ = view_matrix_;
        prev_projection_matrix_ = projection_matrix_;
    }

}
