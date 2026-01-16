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

        /* Transition G-buffer images to GENERAL for compute write */
        VkImageMemoryBarrier barriers[5]{};
        for (int i = 0; i < 4; i++)
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
        barriers[4].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[4].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[4].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[4].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[4].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[4].image = motion_vector_image_;
        barriers[4].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[4].subresourceRange.baseMipLevel = 0;
        barriers[4].subresourceRange.levelCount = 1;
        barriers[4].subresourceRange.baseArrayLayer = 0;
        barriers[4].subresourceRange.layerCount = 1;
        barriers[4].srcAccessMask = 0;
        barriers[4].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 5, barriers);

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
        pc.pad1 = 0.0f;
        pc.grid_size[0] = vol->chunks_x * CHUNK_SIZE;
        pc.grid_size[1] = vol->chunks_y * CHUNK_SIZE;
        pc.grid_size[2] = vol->chunks_z * CHUNK_SIZE;
        pc.total_chunks = vol->total_chunks;
        pc.chunks_dim[0] = vol->chunks_x;
        pc.chunks_dim[1] = vol->chunks_y;
        pc.chunks_dim[2] = vol->chunks_z;
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.rt_quality = rt_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = 512;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.object_count = object_count;

        vkCmdPushConstants(cmd, gbuffer_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

        /* Transition G-buffer images to COLOR_ATTACHMENT for voxel objects render pass */
        for (int i = 0; i < 4; i++)
        {
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[i].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        /* Motion vector image transition */
        barriers[4].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[4].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[4].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barriers[4].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 5, barriers);

        gbuffer_compute_dispatched_ = true;
    }

    void Renderer::dispatch_shadow_compute()
    {
        if (!compute_resources_initialized_ || !shadow_compute_pipeline_ || !shadow_volume_view_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition shadow output to GENERAL for compute write */
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
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        /* Bind pipeline and descriptor sets */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shadow_compute_pipeline_);

        VkDescriptorSet sets[3] = {
            shadow_compute_input_sets_[current_frame_],
            shadow_compute_gbuffer_sets_[current_frame_],
            shadow_compute_output_sets_[current_frame_]};

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                shadow_compute_layout_, 0, 3, sets, 0, nullptr);

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
        pc.pad1 = 0.0f;
        pc.grid_size[0] = deferred_grid_size_[0];
        pc.grid_size[1] = deferred_grid_size_[1];
        pc.grid_size[2] = deferred_grid_size_[2];
        pc.total_chunks = deferred_total_chunks_;
        pc.chunks_dim[0] = deferred_chunks_dim_[0];
        pc.chunks_dim[1] = deferred_chunks_dim_[1];
        pc.chunks_dim[2] = deferred_chunks_dim_[2];
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc.rt_quality = rt_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = 512;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.object_count = 0;

        vkCmdPushConstants(cmd, shadow_compute_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch compute shader */
        uint32_t group_x = (swapchain_extent_.width + 7) / 8;
        uint32_t group_y = (swapchain_extent_.height + 7) / 8;
        vkCmdDispatch(cmd, group_x, group_y, 1);

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
        pc.rt_quality = rt_quality_;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.reserved[0] = temporal_shadow_history_valid_ ? 1 : 0;

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
            vkDestroyImage(device_, shadow_output_image_, nullptr);
            shadow_output_image_ = VK_NULL_HANDLE;
        }
        if (shadow_output_memory_)
        {
            vkFreeMemory(device_, shadow_output_memory_, nullptr);
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

        compute_resources_initialized_ = false;
    }

}
