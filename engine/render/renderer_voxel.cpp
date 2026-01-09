#include "renderer.h"
#include "gpu_volume.h"
#include "voxel_push_constants.h"
#include "engine/core/profile.h"
#include "engine/voxel/volume.h"
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
        }

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
                    destroy_buffer(&voxel_temporal_ubo_[i]);
                }
            }

            if (!create_voxel_descriptors(vol->total_chunks))
            {
                return;
            }

            voxel_resources_initialized_ = true;

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

        void *mapped = nullptr;
        int32_t upload_count = material_count_ > 0 ? material_count_ : 1;
        VkDeviceSize palette_upload_size = static_cast<VkDeviceSize>(upload_count) * sizeof(GPUMaterialColor);
        vkMapMemory(device_, voxel_material_buffer_.memory, 0, palette_upload_size, 0, &mapped);
        memcpy(mapped, &palette, palette_upload_size);
        vkUnmapMemory(device_, voxel_material_buffer_.memory);

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

        uint8_t *voxel_mapped = nullptr;
        vkMapMemory(device_, staging_voxels.memory, 0, voxel_data_size, 0, reinterpret_cast<void **>(&voxel_mapped));
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
        vkUnmapMemory(device_, staging_voxels.memory);

        GPUChunkHeader *headers_mapped = nullptr;
        vkMapMemory(device_, staging_headers.memory, 0, headers_size, 0, reinterpret_cast<void **>(&headers_mapped));
        for (int32_t ci = 0; ci < vol->total_chunks; ci++)
        {
            headers_mapped[ci] = gpu_chunk_header_from_chunk(&vol->chunks[ci]);
        }
        /* DEBUG: Show chunk 0 header */
        printf("  DEBUG: Chunk 0 header: level0_lo=%08x level0_hi=%08x packed=%08x\n",
               headers_mapped[0].level0_lo, headers_mapped[0].level0_hi, headers_mapped[0].packed);
        printf("  DEBUG: Chunk 0 has_any=%d solid_count=%d\n",
               (headers_mapped[0].packed & 0xFF), (headers_mapped[0].packed >> 16));
        vkUnmapMemory(device_, staging_headers.memory);

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
            }

            upload_shadow_volume(mip0.data(), w0, h0, d0,
                                 mip1.data(), w1, h1, d1,
                                 mip2.data(), w2, h2, d2);

            shadow_volume_last_frame_ = vol->current_frame;
        }

        PROFILE_END(PROFILE_VOLUME_INIT);
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

        VkDeviceSize staging_voxel_size = static_cast<VkDeviceSize>(dirty_count) * GPU_CHUNK_DATA_SIZE;
        VkDeviceSize staging_header_size = static_cast<VkDeviceSize>(dirty_count) * sizeof(GPUChunkHeader);

        VulkanBuffer staging_voxels{};
        create_buffer(staging_voxel_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_voxels);

        VulkanBuffer staging_headers{};
        create_buffer(staging_header_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging_headers);

        std::vector<VkBufferCopy> voxel_copies;
        std::vector<VkBufferCopy> header_copies;
        voxel_copies.reserve(dirty_count);
        header_copies.reserve(dirty_count);

        uint8_t *voxel_mapped = nullptr;
        vkMapMemory(device_, staging_voxels.memory, 0, staging_voxel_size, 0, reinterpret_cast<void **>(&voxel_mapped));

        GPUChunkHeader *headers_mapped = nullptr;
        vkMapMemory(device_, staging_headers.memory, 0, staging_header_size, 0, reinterpret_cast<void **>(&headers_mapped));

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

        vkUnmapMemory(device_, staging_voxels.memory);
        vkUnmapMemory(device_, staging_headers.memory);

        vkResetCommandBuffer(upload_cmd_, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(upload_cmd_, &begin_info);

        if (!voxel_copies.empty())
        {
            vkCmdCopyBuffer(upload_cmd_, staging_voxels.buffer, voxel_data_buffer_.buffer,
                            static_cast<uint32_t>(voxel_copies.size()), voxel_copies.data());
        }

        if (!header_copies.empty())
        {
            vkCmdCopyBuffer(upload_cmd_, staging_headers.buffer, voxel_headers_buffer_.buffer,
                            static_cast<uint32_t>(header_copies.size()), header_copies.data());
        }

        vkEndCommandBuffer(upload_cmd_);

        uint64_t signal_value = ++upload_timeline_value_;

        VkTimelineSemaphoreSubmitInfo timeline_submit{};
        timeline_submit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline_submit.signalSemaphoreValueCount = 1;
        timeline_submit.pSignalSemaphoreValues = &signal_value;

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = &timeline_submit;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &upload_cmd_;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &upload_timeline_semaphore_;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);

        pending_destroys_[pending_destroy_count_++] = {staging_voxels, signal_value};
        pending_destroys_[pending_destroy_count_++] = {staging_headers, signal_value};

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

}
