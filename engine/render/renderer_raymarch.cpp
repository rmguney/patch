#include "renderer.h"
#include "voxel_push_constants.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    void Renderer::dispatch_gbuffer_compute(const VoxelVolume *vol, int32_t object_count)
    {
        if (!compute_resources_initialized_ || !gbuffer_compute_pipeline_ || !vol)
            return;

        terrain_draw_count_++;

        /* Cache volume parameters for deferred lighting */
        deferred_bounds_min_[0] = vol->bounds.min_x;
        deferred_bounds_min_[1] = vol->bounds.min_y;
        deferred_bounds_min_[2] = vol->bounds.min_z;
        deferred_bounds_max_[0] = vol->bounds.max_x;
        deferred_bounds_max_[1] = vol->bounds.max_y;
        deferred_bounds_max_[2] = vol->bounds.max_z;
        deferred_voxel_size_ = vol->voxel_size;
        deferred_grid_size_[0] = vol->chunks_x * CHUNK_SIZE;
        deferred_grid_size_[1] = vol->chunks_y * CHUNK_SIZE;
        deferred_grid_size_[2] = vol->chunks_z * CHUNK_SIZE;
        deferred_total_chunks_ = vol->total_chunks;
        deferred_chunks_dim_[0] = vol->chunks_x;
        deferred_chunks_dim_[1] = vol->chunks_y;
        deferred_chunks_dim_[2] = vol->chunks_z;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition all G-buffer images + motion vectors to GENERAL for compute write */
        VkImageMemoryBarrier barriers[GBUFFER_COUNT + 1]{};
        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = gbuffer_images_[i];
            barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[i].subresourceRange.baseMipLevel = 0;
            barriers[i].subresourceRange.levelCount = 1;
            barriers[i].subresourceRange.baseArrayLayer = 0;
            barriers[i].subresourceRange.layerCount = 1;
            barriers[i].srcAccessMask = 0;
            barriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        }
        /* Motion vector image barrier */
        barriers[GBUFFER_COUNT].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[GBUFFER_COUNT].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[GBUFFER_COUNT].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[GBUFFER_COUNT].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[GBUFFER_COUNT].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[GBUFFER_COUNT].image = motion_vector_image_;
        barriers[GBUFFER_COUNT].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[GBUFFER_COUNT].subresourceRange.baseMipLevel = 0;
        barriers[GBUFFER_COUNT].subresourceRange.levelCount = 1;
        barriers[GBUFFER_COUNT].subresourceRange.baseArrayLayer = 0;
        barriers[GBUFFER_COUNT].subresourceRange.layerCount = 1;
        barriers[GBUFFER_COUNT].srcAccessMask = 0;
        barriers[GBUFFER_COUNT].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, GBUFFER_COUNT + 1, barriers);

        /* Bind pipeline and descriptor sets */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gbuffer_compute_pipeline_);

        VkDescriptorSet sets[3] = {
            gbuffer_compute_terrain_sets_[current_frame_],
            gbuffer_compute_vobj_sets_[current_frame_],
            gbuffer_compute_output_sets_[current_frame_]};

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                gbuffer_compute_layout_, 0, 3, sets, 0, nullptr);

        /* Push constants */
        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.bounds_min[0] = vol->bounds.min_x;
        pc.bounds_min[1] = vol->bounds.min_y;
        pc.bounds_min[2] = vol->bounds.min_z;
        pc.voxel_size = vol->voxel_size;
        pc.bounds_max[0] = vol->bounds.max_x;
        pc.bounds_max[1] = vol->bounds.max_y;
        pc.bounds_max[2] = vol->bounds.max_z;
        pc.chunk_size = static_cast<float>(CHUNK_SIZE);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.history_valid = 0;
        pc.grid_size[0] = vol->chunks_x * CHUNK_SIZE;
        pc.grid_size[1] = vol->chunks_y * CHUNK_SIZE;
        pc.grid_size[2] = vol->chunks_z * CHUNK_SIZE;
        pc.total_chunks = vol->total_chunks;
        pc.chunks_dim[0] = vol->chunks_x;
        pc.chunks_dim[1] = vol->chunks_y;
        pc.chunks_dim[2] = vol->chunks_z;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.object_shadow_quality = object_shadow_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = RAYMARCH_MAX_STEPS;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.object_count = object_count;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;

        vkCmdPushConstants(cmd, gbuffer_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition G-buffer images to COLOR_ATTACHMENT for voxel objects render pass */
        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[i].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        /* Motion vector image transition */
        barriers[GBUFFER_COUNT].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[GBUFFER_COUNT].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[GBUFFER_COUNT].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[GBUFFER_COUNT].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, GBUFFER_COUNT + 1, barriers);

        gbuffer_compute_dispatched_ = true;
    }

    void Renderer::dispatch_shadow_compute()
    {
        if (!compute_resources_initialized_ || !shadow_compute_pipeline_ || !shadow_volume_view_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        if (timestamps_supported_)
        {
            uint32_t query_offset = current_frame_ * GPU_TIMESTAMP_COUNT;
            vkCmdWriteTimestamp(cmd,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                timestamp_query_pool_, query_offset + 0);
        }

        /* Transition shadow output to GENERAL for compute write.
           Wait for both G-buffer compute AND render pass to finish before reading G-buffer. */
        VkMemoryBarrier mem_barrier{};
        mem_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mem_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = shadow_output_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mem_barrier, 0, nullptr, 1, &barrier);

        /* Bind pipeline and descriptor sets (set 3 = vobj data for direct object shadow tracing) */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_compute_pipeline_);

        VkDescriptorSet sets[4] = {
            shadow_compute_input_sets_[current_frame_],
            shadow_compute_gbuffer_sets_[current_frame_],
            shadow_compute_output_sets_[current_frame_],
            gbuffer_compute_vobj_sets_[current_frame_]};

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                shadow_compute_layout_, 0, 4, sets, 0, nullptr);

        /* Push constants - same as gbuffer for consistency */
        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.bounds_min[0] = deferred_bounds_min_[0];
        pc.bounds_min[1] = deferred_bounds_min_[1];
        pc.bounds_min[2] = deferred_bounds_min_[2];
        pc.voxel_size = deferred_voxel_size_;
        pc.bounds_max[0] = deferred_bounds_max_[0];
        pc.bounds_max[1] = deferred_bounds_max_[1];
        pc.bounds_max[2] = deferred_bounds_max_[2];
        pc.chunk_size = static_cast<float>(CHUNK_SIZE);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.history_valid = 0;
        pc.grid_size[0] = deferred_grid_size_[0];
        pc.grid_size[1] = deferred_grid_size_[1];
        pc.grid_size[2] = deferred_grid_size_[2];
        pc.total_chunks = deferred_total_chunks_;
        pc.chunks_dim[0] = deferred_chunks_dim_[0];
        pc.chunks_dim[1] = deferred_chunks_dim_[1];
        pc.chunks_dim[2] = deferred_chunks_dim_[2];
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.object_shadow_quality = object_shadow_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = RAYMARCH_MAX_STEPS;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.object_count = vobj_visible_count_; /* Use visible count since metadata buffer is compacted */
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;

        vkCmdPushConstants(cmd, shadow_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        if (timestamps_supported_)
        {
            uint32_t query_offset = current_frame_ * GPU_TIMESTAMP_COUNT;
            vkCmdWriteTimestamp(cmd,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                timestamp_query_pool_, query_offset + 1);
        }

        /* Transition shadow output to SHADER_READ_ONLY for sampling in lighting pass */
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void Renderer::dispatch_temporal_shadow_resolve()
    {
        if (!compute_resources_initialized_ || !temporal_compute_pipeline_ || !history_image_views_[0] || !history_image_views_[1])
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        const int write_index = history_write_index_ & 1;
        const int read_index = 1 - write_index;

        /* Transition write history image to GENERAL for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = history_images_[write_index];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Update history descriptors for this frame */
        VkDescriptorImageInfo shadow_history_info{};
        shadow_history_info.sampler = gbuffer_sampler_;
        shadow_history_info.imageView = history_image_views_[read_index];
        shadow_history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet history_write{};
        history_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        history_write.dstSet = temporal_shadow_input_sets_[current_frame_];
        history_write.dstBinding = 4;
        history_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        history_write.descriptorCount = 1;
        history_write.pImageInfo = &shadow_history_info;

        VkDescriptorImageInfo out_info{};
        out_info.imageView = history_image_views_[write_index];
        out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet out_write{};
        out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out_write.dstSet = temporal_shadow_output_sets_[current_frame_];
        out_write.dstBinding = 0;
        out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        out_write.descriptorCount = 1;
        out_write.pImageInfo = &out_info;

        VkWriteDescriptorSet writes[2] = {history_write, out_write};
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_compute_pipeline_);
        VkDescriptorSet sets[2] = {temporal_shadow_input_sets_[current_frame_], temporal_shadow_output_sets_[current_frame_]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_compute_layout_, 0, 2, sets, 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.object_shadow_quality = object_shadow_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;
        pc.history_valid = (temporal_shadow_history_valid_ ? 1 : 0) | 0;

        vkCmdPushConstants(cmd, temporal_compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition resolved image to SHADER_READ_ONLY for lighting pass sampling */
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Point deferred lighting at the resolved shadow image for this frame */
        update_deferred_shadow_buffer_descriptor(current_frame_, history_image_views_[write_index]);

        temporal_shadow_history_valid_ = true;
        history_write_index_ = read_index;
    }

    void Renderer::dispatch_ao_compute()
    {
        if (!ao_resources_initialized_ || !ao_compute_pipeline_ || !shadow_volume_view_ ||
            !ao_compute_descriptor_pool_ || !ao_output_image_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition AO output to GENERAL for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = ao_output_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Bind pipeline and descriptor sets */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ao_compute_pipeline_);

        VkDescriptorSet sets[3] = {
            ao_compute_input_sets_[current_frame_],
            ao_compute_gbuffer_sets_[current_frame_],
            ao_compute_output_sets_[current_frame_]};

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                ao_compute_layout_, 0, 3, sets, 0, nullptr);

        /* Push constants - same structure as shadow */
        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.bounds_min[0] = deferred_bounds_min_[0];
        pc.bounds_min[1] = deferred_bounds_min_[1];
        pc.bounds_min[2] = deferred_bounds_min_[2];
        pc.voxel_size = deferred_voxel_size_;
        pc.bounds_max[0] = deferred_bounds_max_[0];
        pc.bounds_max[1] = deferred_bounds_max_[1];
        pc.bounds_max[2] = deferred_bounds_max_[2];
        pc.chunk_size = static_cast<float>(CHUNK_SIZE);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.history_valid = 0;
        pc.grid_size[0] = deferred_grid_size_[0];
        pc.grid_size[1] = deferred_grid_size_[1];
        pc.grid_size[2] = deferred_grid_size_[2];
        pc.total_chunks = deferred_total_chunks_;
        pc.chunks_dim[0] = deferred_chunks_dim_[0];
        pc.chunks_dim[1] = deferred_chunks_dim_[1];
        pc.chunks_dim[2] = deferred_chunks_dim_[2];
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.object_shadow_quality = object_shadow_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = RAYMARCH_MAX_STEPS;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.object_count = 0;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;

        vkCmdPushConstants(cmd, ao_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition AO output to SHADER_READ_ONLY for temporal resolve */
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void Renderer::dispatch_temporal_ao_resolve()
    {
        if (!ao_resources_initialized_ || !temporal_ao_compute_pipeline_ ||
            !ao_history_image_views_[0] || !ao_history_image_views_[1] ||
            !temporal_ao_descriptor_pool_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        const int write_index = ao_history_write_index_ & 1;
        const int read_index = 1 - write_index;

        /* Transition write history image to GENERAL for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = ao_history_images_[write_index];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Update history descriptors for this frame */
        VkDescriptorImageInfo ao_history_info{};
        ao_history_info.sampler = gbuffer_sampler_;
        ao_history_info.imageView = ao_history_image_views_[read_index];
        ao_history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet history_write{};
        history_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        history_write.dstSet = temporal_ao_input_sets_[current_frame_];
        history_write.dstBinding = 4;
        history_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        history_write.descriptorCount = 1;
        history_write.pImageInfo = &ao_history_info;

        VkDescriptorImageInfo out_info{};
        out_info.imageView = ao_history_image_views_[write_index];
        out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet out_write{};
        out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out_write.dstSet = temporal_ao_output_sets_[current_frame_];
        out_write.dstBinding = 0;
        out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        out_write.descriptorCount = 1;
        out_write.pImageInfo = &out_info;

        VkWriteDescriptorSet writes[2] = {history_write, out_write};
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_ao_compute_pipeline_);
        VkDescriptorSet sets[2] = {temporal_ao_input_sets_[current_frame_], temporal_ao_output_sets_[current_frame_]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal_ao_compute_layout_, 0, 2, sets, 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.object_shadow_quality = object_shadow_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;
        pc.history_valid = (temporal_ao_history_valid_ ? 1 : 0) | 0;

        vkCmdPushConstants(cmd, temporal_ao_compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition resolved image to SHADER_READ_ONLY for lighting pass sampling */
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Point deferred lighting at the resolved AO image for this frame */
        update_deferred_ao_buffer_descriptor(current_frame_, ao_history_image_views_[write_index]);

        temporal_ao_history_valid_ = true;
        ao_history_write_index_ = read_index;
    }

    void Renderer::dispatch_taa_resolve()
    {
        if (!taa_initialized_ || !taa_compute_pipeline_ || !taa_history_views_[0] || !taa_history_views_[1])
            return;

        if (!lit_color_view_ || !motion_vector_view_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        const int write_index = taa_history_write_index_ & 1;
        const int read_index = 1 - write_index;

        /* Transition write history image to GENERAL for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = taa_history_images_[write_index];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Transition lit_color to shader read (from color attachment) */
        VkImageMemoryBarrier lit_barrier{};
        lit_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        lit_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        lit_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lit_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lit_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lit_barrier.image = lit_color_image_;
        lit_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        lit_barrier.subresourceRange.baseMipLevel = 0;
        lit_barrier.subresourceRange.levelCount = 1;
        lit_barrier.subresourceRange.baseArrayLayer = 0;
        lit_barrier.subresourceRange.layerCount = 1;
        lit_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        lit_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &lit_barrier);

        /* Update descriptors for this frame's ping-pong buffers */
        VkDescriptorImageInfo current_info{};
        current_info.sampler = gbuffer_sampler_;
        current_info.imageView = lit_color_view_;
        current_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo history_info{};
        history_info.sampler = gbuffer_sampler_;
        history_info.imageView = taa_history_views_[read_index];
        history_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo motion_info{};
        motion_info.sampler = gbuffer_sampler_;
        motion_info.imageView = motion_vector_view_;
        motion_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet input_writes[3]{};
        input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        input_writes[0].dstSet = taa_input_sets_[current_frame_];
        input_writes[0].dstBinding = 0;
        input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        input_writes[0].descriptorCount = 1;
        input_writes[0].pImageInfo = &current_info;

        input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        input_writes[1].dstSet = taa_input_sets_[current_frame_];
        input_writes[1].dstBinding = 1;
        input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        input_writes[1].descriptorCount = 1;
        input_writes[1].pImageInfo = &history_info;

        input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        input_writes[2].dstSet = taa_input_sets_[current_frame_];
        input_writes[2].dstBinding = 2;
        input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        input_writes[2].descriptorCount = 1;
        input_writes[2].pImageInfo = &motion_info;

        VkDescriptorImageInfo out_info{};
        out_info.imageView = taa_history_views_[write_index];
        out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet out_write{};
        out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out_write.dstSet = taa_output_sets_[current_frame_];
        out_write.dstBinding = 0;
        out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        out_write.descriptorCount = 1;
        out_write.pImageInfo = &out_info;

        vkUpdateDescriptorSets(device_, 3, input_writes, 0, nullptr);
        vkUpdateDescriptorSets(device_, 1, &out_write, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, taa_compute_pipeline_);
        VkDescriptorSet sets[2] = {taa_input_sets_[current_frame_], taa_output_sets_[current_frame_]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, taa_compute_layout_, 0, 2, sets, 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.history_valid = taa_history_valid_ ? 1 : 0;

        vkCmdPushConstants(cmd, taa_compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition TAA output to SHADER_READ_ONLY for denoise sampling */
        barrier.image = taa_history_images_[write_index];
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Update denoise input to read from TAA output instead of lit_color */
        update_denoise_color_input(current_frame_, taa_history_views_[write_index]);

        taa_history_valid_ = true;
        taa_history_write_index_ = read_index;
    }

    void Renderer::destroy_compute_raymarching_resources()
    {
        if (!compute_resources_initialized_)
            return;

        vkDeviceWaitIdle(device_);

        if (shadow_output_view_)
        {
            vkDestroyImageView(device_, shadow_output_view_, nullptr);
            shadow_output_view_ = VK_NULL_HANDLE;
        }
        if (shadow_output_image_)
        {
            gpu_allocator_.destroy_image(shadow_output_image_, shadow_output_memory_);
            shadow_output_image_ = VK_NULL_HANDLE;
            shadow_output_memory_ = VK_NULL_HANDLE;
        }

        if (gbuffer_compute_pipeline_)
        {
            vkDestroyPipeline(device_, gbuffer_compute_pipeline_, nullptr);
            gbuffer_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, gbuffer_compute_layout_, nullptr);
            gbuffer_compute_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_terrain_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_compute_terrain_layout_, nullptr);
            gbuffer_compute_terrain_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_vobj_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_compute_vobj_layout_, nullptr);
            gbuffer_compute_vobj_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gbuffer_compute_output_layout_, nullptr);
            gbuffer_compute_output_layout_ = VK_NULL_HANDLE;
        }
        if (gbuffer_compute_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, gbuffer_compute_descriptor_pool_, nullptr);
            gbuffer_compute_descriptor_pool_ = VK_NULL_HANDLE;
        }

        if (shadow_compute_pipeline_)
        {
            vkDestroyPipeline(device_, shadow_compute_pipeline_, nullptr);
            shadow_compute_pipeline_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_layout_)
        {
            vkDestroyPipelineLayout(device_, shadow_compute_layout_, nullptr);
            shadow_compute_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_input_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, shadow_compute_input_layout_, nullptr);
            shadow_compute_input_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_gbuffer_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, shadow_compute_gbuffer_layout_, nullptr);
            shadow_compute_gbuffer_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, shadow_compute_output_layout_, nullptr);
            shadow_compute_output_layout_ = VK_NULL_HANDLE;
        }
        if (shadow_compute_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, shadow_compute_descriptor_pool_, nullptr);
            shadow_compute_descriptor_pool_ = VK_NULL_HANDLE;
        }

        /* Destroy AO resources */
        destroy_ao_resources();

        /* Destroy spatial denoise resources */
        destroy_spatial_denoise_resources();

        /* Destroy TAA resources */
        destroy_taa_resources();

        compute_resources_initialized_ = false;
    }

    void Renderer::dispatch_spatial_denoise()
    {
        if (!spatial_denoise_initialized_ || !spatial_denoise_pipeline_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Barrier: lit_color to SHADER_READ, denoised_color to GENERAL */
        /* Note: render_pass_ transitions color attachment to PRESENT_SRC_KHR */
        VkImageMemoryBarrier barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = lit_color_image_;
        barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[1].srcAccessMask = 0;
        barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image = denoised_color_image_;
        barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 2, barriers);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatial_denoise_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatial_denoise_layout_,
                                0, 1, &spatial_denoise_input_sets_[current_frame_], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatial_denoise_layout_,
                                1, 1, &spatial_denoise_output_sets_[current_frame_], 0, nullptr);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
        Mat4 inv_proj = mat4_inverse(projection_matrix_);

        VoxelPushConstants pc{};
        pc.inv_view = inv_view;
        pc.inv_projection = inv_proj;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.debug_mode = 0;

        vkCmdPushConstants(cmd, spatial_denoise_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t groups_x = (swapchain_extent_.width + 7) / 8;
        uint32_t groups_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, groups_x, groups_y, 1);

        /* Barrier: denoised_color ready for transfer */
        VkImageMemoryBarrier transfer_barrier{};
        transfer_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        transfer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        transfer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        transfer_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        transfer_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        transfer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transfer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transfer_barrier.image = denoised_color_image_;
        transfer_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &transfer_barrier);
    }

    void Renderer::blit_denoised_to_swapchain(uint32_t image_index)
    {
        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition swapchain to TRANSFER_DST */
        VkImageMemoryBarrier dst_barrier{};
        dst_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dst_barrier.srcAccessMask = 0;
        dst_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dst_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dst_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dst_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dst_barrier.image = swapchain_images_[image_index];
        dst_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &dst_barrier);

        /* Blit denoised to swapchain */
        VkImageBlit blit_region{};
        blit_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit_region.srcOffsets[0] = {0, 0, 0};
        blit_region.srcOffsets[1] = {static_cast<int32_t>(swapchain_extent_.width),
                                     static_cast<int32_t>(swapchain_extent_.height), 1};
        blit_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit_region.dstOffsets[0] = {0, 0, 0};
        blit_region.dstOffsets[1] = {static_cast<int32_t>(swapchain_extent_.width),
                                     static_cast<int32_t>(swapchain_extent_.height), 1};

        vkCmdBlitImage(cmd,
                       denoised_color_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchain_images_[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit_region, VK_FILTER_NEAREST);

        /* Transition swapchain to COLOR_ATTACHMENT for UI */
        VkImageMemoryBarrier present_barrier{};
        present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        present_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        present_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        present_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        present_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        present_barrier.image = swapchain_images_[image_index];
        present_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &present_barrier);
    }

}
