#include "renderer.h"
#include "shaders_embedded.h"
#include "voxel_push_constants.h"
#include <cstdio>

namespace patch
{

    bool Renderer::create_gi_cascade_resources()
    {
        if (gi_resources_initialized_)
            return true;

        printf("Creating GI cascade resources...\n");

        /* Create sampler for cascade textures (trilinear filtering) */
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0f;

        if (vkCreateSampler(device_, &sampler_info, nullptr, &gi_cascade_sampler_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI cascade sampler\n");
            return false;
        }

        /* Create cascade textures at decreasing resolutions
           Level 0: 1 voxel per texel (highest resolution)
           Level 1: 2x2x2 voxels per texel
           Level 2: 4x4x4 voxels per texel
           Level 3: 8x8x8 voxels per texel (lowest resolution, covers full volume) */

        size_t total_memory = 0;

        for (uint32_t level = 0; level < GI_CASCADE_LEVELS; level++)
        {
            uint32_t voxels_per_texel = 1u << level; /* 1, 2, 4, 8 */
            uint32_t dim = GI_CASCADE_BASE_DIM >> level; /* 128, 64, 32, 16 */

            if (dim < 8)
                dim = 8; /* Minimum dimension */

            gi_cascades_[level].dims[0] = dim;
            gi_cascades_[level].dims[1] = dim;
            gi_cascades_[level].dims[2] = dim;
            gi_cascades_[level].voxels_per_texel = voxels_per_texel;

            /* Create 3D texture for this cascade level
               RGBA16F for HDR radiance (RGB) + direction encoding (A) */
            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_3D;
            image_info.extent.width = dim;
            image_info.extent.height = dim;
            image_info.extent.depth = dim;
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(device_, &image_info, nullptr, &gi_cascades_[level].image) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create GI cascade image level %u\n", level);
                destroy_gi_cascade_resources();
                return false;
            }

            VkMemoryRequirements mem_reqs;
            vkGetImageMemoryRequirements(device_, gi_cascades_[level].image, &mem_reqs);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_reqs.size;
            alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device_, &alloc_info, nullptr, &gi_cascades_[level].memory) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI cascade memory level %u\n", level);
                destroy_gi_cascade_resources();
                return false;
            }

            vkBindImageMemory(device_, gi_cascades_[level].image, gi_cascades_[level].memory, 0);

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = gi_cascades_[level].image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
            view_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_info, nullptr, &gi_cascades_[level].view) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to create GI cascade view level %u\n", level);
                destroy_gi_cascade_resources();
                return false;
            }

            total_memory += mem_reqs.size;
            printf("  Cascade level %u: %ux%ux%u (%u voxels/texel), %.2f MB\n",
                   level, dim, dim, dim, voxels_per_texel,
                   static_cast<float>(mem_reqs.size) / (1024.0f * 1024.0f));
        }

        printf("  Total GI cascade memory: %.2f MB\n", static_cast<float>(total_memory) / (1024.0f * 1024.0f));

        gi_cascade_needs_full_rebuild_ = true;
        gi_resources_initialized_ = true;

        return true;
    }

    void Renderer::destroy_gi_cascade_resources()
    {
        vkDeviceWaitIdle(device_);

        for (uint32_t level = 0; level < GI_CASCADE_LEVELS; level++)
        {
            if (gi_cascades_[level].view)
            {
                vkDestroyImageView(device_, gi_cascades_[level].view, nullptr);
                gi_cascades_[level].view = VK_NULL_HANDLE;
            }
            if (gi_cascades_[level].image)
            {
                vkDestroyImage(device_, gi_cascades_[level].image, nullptr);
                gi_cascades_[level].image = VK_NULL_HANDLE;
            }
            if (gi_cascades_[level].memory)
            {
                vkFreeMemory(device_, gi_cascades_[level].memory, nullptr);
                gi_cascades_[level].memory = VK_NULL_HANDLE;
            }
            gi_cascades_[level].dims[0] = 0;
            gi_cascades_[level].dims[1] = 0;
            gi_cascades_[level].dims[2] = 0;
            gi_cascades_[level].voxels_per_texel = 1;
        }

        if (gi_cascade_sampler_)
        {
            vkDestroySampler(device_, gi_cascade_sampler_, nullptr);
            gi_cascade_sampler_ = VK_NULL_HANDLE;
        }

        /* Cleanup injection pipeline resources */
        if (gi_inject_pipeline_)
        {
            vkDestroyPipeline(device_, gi_inject_pipeline_, nullptr);
            gi_inject_pipeline_ = VK_NULL_HANDLE;
        }
        if (gi_inject_layout_)
        {
            vkDestroyPipelineLayout(device_, gi_inject_layout_, nullptr);
            gi_inject_layout_ = VK_NULL_HANDLE;
        }
        if (gi_inject_input_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_inject_input_layout_, nullptr);
            gi_inject_input_layout_ = VK_NULL_HANDLE;
        }
        if (gi_inject_output_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_inject_output_layout_, nullptr);
            gi_inject_output_layout_ = VK_NULL_HANDLE;
        }
        if (gi_inject_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, gi_inject_descriptor_pool_, nullptr);
            gi_inject_descriptor_pool_ = VK_NULL_HANDLE;
        }

        /* Cleanup propagation pipeline resources */
        if (gi_propagate_pipeline_)
        {
            vkDestroyPipeline(device_, gi_propagate_pipeline_, nullptr);
            gi_propagate_pipeline_ = VK_NULL_HANDLE;
        }
        if (gi_propagate_layout_)
        {
            vkDestroyPipelineLayout(device_, gi_propagate_layout_, nullptr);
            gi_propagate_layout_ = VK_NULL_HANDLE;
        }
        if (gi_propagate_src_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_propagate_src_layout_, nullptr);
            gi_propagate_src_layout_ = VK_NULL_HANDLE;
        }
        if (gi_propagate_dst_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, gi_propagate_dst_layout_, nullptr);
            gi_propagate_dst_layout_ = VK_NULL_HANDLE;
        }
        if (gi_propagate_descriptor_pool_)
        {
            vkDestroyDescriptorPool(device_, gi_propagate_descriptor_pool_, nullptr);
            gi_propagate_descriptor_pool_ = VK_NULL_HANDLE;
        }

        clear_gi_dirty_flags();
        gi_resources_initialized_ = false;
    }

    void Renderer::mark_gi_cascade_dirty(uint32_t level, uint32_t x, uint32_t y, uint32_t z)
    {
        if (level >= GI_CASCADE_LEVELS)
            return;

        uint32_t dim = gi_cascades_[level].dims[0];
        if (x >= dim || y >= dim || z >= dim)
            return;

        uint32_t index = z * dim * dim + y * dim + x;
        uint32_t word = index / 64;
        uint32_t bit = index % 64;

        if (word < GI_DIRTY_BITMAP_SIZE)
        {
            gi_dirty_bitmap_[level][word] |= (1ULL << bit);
        }
    }

    void Renderer::clear_gi_dirty_flags()
    {
        for (uint32_t level = 0; level < GI_CASCADE_LEVELS; level++)
        {
            for (uint32_t i = 0; i < GI_DIRTY_BITMAP_SIZE; i++)
            {
                gi_dirty_bitmap_[level][i] = 0;
            }
        }
        gi_cascade_needs_full_rebuild_ = false;
    }

    size_t Renderer::get_gi_cascade_memory_usage() const
    {
        size_t total = 0;
        for (uint32_t level = 0; level < GI_CASCADE_LEVELS; level++)
        {
            if (gi_cascades_[level].image)
            {
                uint32_t dim = gi_cascades_[level].dims[0];
                /* RGBA16F = 8 bytes per texel */
                total += static_cast<size_t>(dim) * dim * dim * 8;
            }
        }
        return total;
    }

    void Renderer::set_gi_quality(int level)
    {
        if (level < 0)
            level = 0;
        if (level > 3)
            level = 3;

        if (gi_quality_ != level)
        {
            gi_quality_ = level;

            /* Only create GI resources if compute resources are ready (scene loaded).
             * This prevents initialization order issues where GI is created before
             * the shadow volume and other compute dependencies exist. GI will be
             * created later via init_gi_if_pending() when init_volume_for_raymarching runs. */
            if (level > 0 && !gi_resources_initialized_ && compute_resources_initialized_)
            {
                if (create_gi_cascade_resources())
                {
                    create_gi_inject_pipeline();
                    create_gi_inject_descriptor_sets();
                    create_gi_propagate_pipeline();
                    create_gi_propagate_descriptor_sets();
                    update_deferred_gi_cascade_descriptors();
                }
            }
            else if (level == 0 && gi_resources_initialized_)
            {
                destroy_gi_cascade_resources();
            }
        }
    }

    void Renderer::init_gi_if_pending()
    {
        /* Initialize GI resources if quality was set before compute resources were ready */
        if (gi_quality_ > 0 && !gi_resources_initialized_ && compute_resources_initialized_)
        {
            if (create_gi_cascade_resources())
            {
                create_gi_inject_pipeline();
                create_gi_inject_descriptor_sets();
                create_gi_propagate_pipeline();
                create_gi_propagate_descriptor_sets();
                update_deferred_gi_cascade_descriptors();
            }
        }
    }

    bool Renderer::create_gi_inject_pipeline()
    {
        /* Set 0: Voxel data + shadow volume + materials (same as reflection) */
        VkDescriptorSetLayoutBinding input_bindings[4]{};

        /* Binding 0: Voxel data SSBO */
        input_bindings[0].binding = 0;
        input_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        input_bindings[0].descriptorCount = 1;
        input_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        /* Binding 1: Chunk headers SSBO */
        input_bindings[1].binding = 1;
        input_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        input_bindings[1].descriptorCount = 1;
        input_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        /* Binding 2: Shadow volume 3D texture */
        input_bindings[2].binding = 2;
        input_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        input_bindings[2].descriptorCount = 1;
        input_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        /* Binding 3: Material palette UBO */
        input_bindings[3].binding = 3;
        input_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        input_bindings[3].descriptorCount = 1;
        input_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo input_layout_info{};
        input_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        input_layout_info.bindingCount = 4;
        input_layout_info.pBindings = input_bindings;

        if (vkCreateDescriptorSetLayout(device_, &input_layout_info, nullptr, &gi_inject_input_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI inject input layout\n");
            return false;
        }

        /* Set 1: Cascade output (storage image) */
        VkDescriptorSetLayoutBinding output_binding{};
        output_binding.binding = 0;
        output_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_binding.descriptorCount = 1;
        output_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo output_layout_info{};
        output_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        output_layout_info.bindingCount = 1;
        output_layout_info.pBindings = &output_binding;

        if (vkCreateDescriptorSetLayout(device_, &output_layout_info, nullptr, &gi_inject_output_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI inject output layout\n");
            return false;
        }

        /* Pipeline layout */
        VkDescriptorSetLayout set_layouts[2] = {gi_inject_input_layout_, gi_inject_output_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = 256;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gi_inject_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI inject pipeline layout\n");
            return false;
        }

        /* Create compute pipeline */
        if (!create_compute_pipeline(
                shaders::k_shader_gi_inject_comp_spv,
                shaders::k_shader_gi_inject_comp_spv_size,
                gi_inject_layout_,
                &gi_inject_pipeline_))
        {
            fprintf(stderr, "Failed to create GI inject compute pipeline\n");
            return false;
        }

        printf("  GI inject pipeline created\n");
        return true;
    }

    bool Renderer::create_gi_inject_descriptor_sets()
    {
        if (!gi_resources_initialized_ || !voxel_data_buffer_.buffer)
            return true;

        /* Create descriptor pool */
        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * 2;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &gi_inject_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI inject descriptor pool\n");
            return false;
        }

        /* Allocate descriptor sets */
        VkDescriptorSetLayout input_layouts[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSetLayout output_layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            input_layouts[i] = gi_inject_input_layout_;
            output_layouts[i] = gi_inject_output_layout_;
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = gi_inject_descriptor_pool_;
        alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;

        alloc_info.pSetLayouts = input_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, gi_inject_input_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate GI inject input sets\n");
            return false;
        }

        alloc_info.pSetLayouts = output_layouts;
        if (vkAllocateDescriptorSets(device_, &alloc_info, gi_inject_output_sets_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to allocate GI inject output sets\n");
            return false;
        }

        update_gi_inject_descriptors();

        printf("  GI inject descriptor sets created\n");
        return true;
    }

    void Renderer::update_gi_inject_descriptors()
    {
        if (!gi_inject_descriptor_pool_ || !voxel_data_buffer_.buffer)
            return;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            /* Set 0: Input data */
            VkDescriptorBufferInfo voxel_data_info{};
            voxel_data_info.buffer = voxel_data_buffer_.buffer;
            voxel_data_info.offset = 0;
            voxel_data_info.range = VK_WHOLE_SIZE;

            VkDescriptorBufferInfo headers_info{};
            headers_info.buffer = voxel_headers_buffer_.buffer;
            headers_info.offset = 0;
            headers_info.range = VK_WHOLE_SIZE;

            VkDescriptorImageInfo shadow_vol_info{};
            shadow_vol_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadow_vol_info.imageView = shadow_volume_view_ ? shadow_volume_view_ : gbuffer_views_[0];
            shadow_vol_info.sampler = shadow_volume_sampler_ ? shadow_volume_sampler_ : gbuffer_sampler_;

            VkDescriptorBufferInfo material_info{};
            material_info.buffer = voxel_material_buffer_.buffer;
            material_info.offset = 0;
            material_info.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet input_writes[4]{};

            input_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[0].dstSet = gi_inject_input_sets_[i];
            input_writes[0].dstBinding = 0;
            input_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            input_writes[0].descriptorCount = 1;
            input_writes[0].pBufferInfo = &voxel_data_info;

            input_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[1].dstSet = gi_inject_input_sets_[i];
            input_writes[1].dstBinding = 1;
            input_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            input_writes[1].descriptorCount = 1;
            input_writes[1].pBufferInfo = &headers_info;

            input_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[2].dstSet = gi_inject_input_sets_[i];
            input_writes[2].dstBinding = 2;
            input_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            input_writes[2].descriptorCount = 1;
            input_writes[2].pImageInfo = &shadow_vol_info;

            input_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            input_writes[3].dstSet = gi_inject_input_sets_[i];
            input_writes[3].dstBinding = 3;
            input_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            input_writes[3].descriptorCount = 1;
            input_writes[3].pBufferInfo = &material_info;

            vkUpdateDescriptorSets(device_, 4, input_writes, 0, nullptr);

            /* Set 1: Cascade output (level 0) */
            if (gi_cascades_[0].view)
            {
                VkDescriptorImageInfo cascade_info{};
                cascade_info.imageView = gi_cascades_[0].view;
                cascade_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkWriteDescriptorSet output_write{};
                output_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                output_write.dstSet = gi_inject_output_sets_[i];
                output_write.dstBinding = 0;
                output_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                output_write.descriptorCount = 1;
                output_write.pImageInfo = &cascade_info;

                vkUpdateDescriptorSets(device_, 1, &output_write, 0, nullptr);
            }
        }
    }

    void Renderer::dispatch_gi_inject()
    {
        if (!gi_resources_initialized_ || !gi_inject_pipeline_ || gi_quality_ == 0)
            return;

        if (!gi_cascades_[0].image || !shadow_volume_view_)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        /* Transition cascade level 0 to general for compute write */
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = gi_cascades_[0].image;
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

        /* Bind pipeline and descriptors */
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gi_inject_pipeline_);

        VkDescriptorSet sets[2] = {gi_inject_input_sets_[current_frame_], gi_inject_output_sets_[current_frame_]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gi_inject_layout_,
                                0, 2, sets, 0, nullptr);

        /* Push constants */
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
        pc.history_valid = (gi_quality_ << 8);
        pc.grid_size[0] = deferred_grid_size_[0];
        pc.grid_size[1] = deferred_grid_size_[1];
        pc.grid_size[2] = deferred_grid_size_[2];
        pc.total_chunks = deferred_total_chunks_;
        pc.chunks_dim[0] = deferred_chunks_dim_[0];
        pc.chunks_dim[1] = deferred_chunks_dim_[1];
        pc.chunks_dim[2] = deferred_chunks_dim_[2];
        pc.frame_count = static_cast<int32_t>(total_frame_count_);
        pc._pad0 = 0;
        pc.debug_mode = terrain_debug_mode_;
        pc.is_orthographic = (projection_mode_ == ProjectionMode::Orthographic) ? 1 : 0;
        pc.max_steps = 512;
        pc.near_plane = 0.1f;
        pc.far_plane = 1000.0f;
        pc.object_count = 0;
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        pc.lod_quality = lod_quality_;
        pc.reflection_quality = reflection_quality_;

        vkCmdPushConstants(cmd, gi_inject_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        /* Dispatch for cascade level 0 (128³ with 4³ workgroups = 32³ dispatches) */
        uint32_t dim = gi_cascades_[0].dims[0];
        uint32_t groups = (dim + 3) / 4;
        vkCmdDispatch(cmd, groups, groups, groups);

        /* Don't transition yet - propagation will read from level 0 */
    }

    bool Renderer::create_gi_propagate_pipeline()
    {
        /* Set 0: Source cascade (sampler) */
        VkDescriptorSetLayoutBinding src_binding{};
        src_binding.binding = 0;
        src_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        src_binding.descriptorCount = 1;
        src_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo src_layout_info{};
        src_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        src_layout_info.bindingCount = 1;
        src_layout_info.pBindings = &src_binding;

        if (vkCreateDescriptorSetLayout(device_, &src_layout_info, nullptr, &gi_propagate_src_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI propagate source layout\n");
            return false;
        }

        /* Set 1: Destination cascade (storage image) */
        VkDescriptorSetLayoutBinding dst_binding{};
        dst_binding.binding = 0;
        dst_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        dst_binding.descriptorCount = 1;
        dst_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dst_layout_info{};
        dst_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dst_layout_info.bindingCount = 1;
        dst_layout_info.pBindings = &dst_binding;

        if (vkCreateDescriptorSetLayout(device_, &dst_layout_info, nullptr, &gi_propagate_dst_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI propagate dest layout\n");
            return false;
        }

        /* Pipeline layout with push constants for dimensions */
        VkDescriptorSetLayout set_layouts[2] = {gi_propagate_src_layout_, gi_propagate_dst_layout_};

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = 32; /* ivec3 src_dims, int src_level, ivec3 dst_dims, int dst_level, float falloff, float conserve, 2 pad */

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &gi_propagate_layout_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI propagate pipeline layout\n");
            return false;
        }

        if (!create_compute_pipeline(
                shaders::k_shader_gi_propagate_comp_spv,
                shaders::k_shader_gi_propagate_comp_spv_size,
                gi_propagate_layout_,
                &gi_propagate_pipeline_))
        {
            fprintf(stderr, "Failed to create GI propagate pipeline\n");
            return false;
        }

        printf("  GI propagate pipeline created\n");
        return true;
    }

    bool Renderer::create_gi_propagate_descriptor_sets()
    {
        if (!gi_resources_initialized_)
            return true;

        /* Create descriptor pool for propagation steps */
        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[0].descriptorCount = GI_PROPAGATE_STEPS;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[1].descriptorCount = GI_PROPAGATE_STEPS;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = GI_PROPAGATE_STEPS * 2;

        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &gi_propagate_descriptor_pool_) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create GI propagate descriptor pool\n");
            return false;
        }

        /* Allocate descriptor sets for each propagation step */
        for (uint32_t step = 0; step < GI_PROPAGATE_STEPS; step++)
        {
            VkDescriptorSetAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = gi_propagate_descriptor_pool_;
            alloc_info.descriptorSetCount = 1;

            alloc_info.pSetLayouts = &gi_propagate_src_layout_;
            if (vkAllocateDescriptorSets(device_, &alloc_info, &gi_propagate_src_sets_[step]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI propagate src set %u\n", step);
                return false;
            }

            alloc_info.pSetLayouts = &gi_propagate_dst_layout_;
            if (vkAllocateDescriptorSets(device_, &alloc_info, &gi_propagate_dst_sets_[step]) != VK_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate GI propagate dst set %u\n", step);
                return false;
            }

            /* Update descriptors: step N reads from cascade[N], writes to cascade[N+1] */
            uint32_t src_level = step;
            uint32_t dst_level = step + 1;

            VkDescriptorImageInfo src_info{};
            src_info.sampler = gi_cascade_sampler_;
            src_info.imageView = gi_cascades_[src_level].view;
            src_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet src_write{};
            src_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            src_write.dstSet = gi_propagate_src_sets_[step];
            src_write.dstBinding = 0;
            src_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            src_write.descriptorCount = 1;
            src_write.pImageInfo = &src_info;

            VkDescriptorImageInfo dst_info{};
            dst_info.imageView = gi_cascades_[dst_level].view;
            dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet dst_write{};
            dst_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            dst_write.dstSet = gi_propagate_dst_sets_[step];
            dst_write.dstBinding = 0;
            dst_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            dst_write.descriptorCount = 1;
            dst_write.pImageInfo = &dst_info;

            VkWriteDescriptorSet writes[2] = {src_write, dst_write};
            vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        }

        printf("  GI propagate descriptor sets created\n");
        return true;
    }

    void Renderer::dispatch_gi_propagate()
    {
        if (!gi_resources_initialized_ || !gi_propagate_pipeline_ || gi_quality_ == 0)
            return;

        VkCommandBuffer cmd = command_buffers_[current_frame_];

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gi_propagate_pipeline_);

        /* Propagate through each level: 0→1, 1→2, 2→3 */
        for (uint32_t step = 0; step < GI_PROPAGATE_STEPS; step++)
        {
            uint32_t src_level = step;
            uint32_t dst_level = step + 1;

            /* Transition source to SHADER_READ if not already */
            VkImageMemoryBarrier src_barrier{};
            src_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            src_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            src_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            src_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            src_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            src_barrier.image = gi_cascades_[src_level].image;
            src_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            src_barrier.subresourceRange.baseMipLevel = 0;
            src_barrier.subresourceRange.levelCount = 1;
            src_barrier.subresourceRange.baseArrayLayer = 0;
            src_barrier.subresourceRange.layerCount = 1;
            src_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            src_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            /* Transition destination to GENERAL for write */
            VkImageMemoryBarrier dst_barrier{};
            dst_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            dst_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dst_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            dst_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dst_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dst_barrier.image = gi_cascades_[dst_level].image;
            dst_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            dst_barrier.subresourceRange.baseMipLevel = 0;
            dst_barrier.subresourceRange.levelCount = 1;
            dst_barrier.subresourceRange.baseArrayLayer = 0;
            dst_barrier.subresourceRange.layerCount = 1;
            dst_barrier.srcAccessMask = 0;
            dst_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            VkImageMemoryBarrier barriers[2] = {src_barrier, dst_barrier};
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, barriers);

            /* Bind descriptor sets for this step */
            VkDescriptorSet sets[2] = {gi_propagate_src_sets_[step], gi_propagate_dst_sets_[step]};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gi_propagate_layout_,
                                    0, 2, sets, 0, nullptr);

            /* Push constants */
            struct PropagatePushConstants
            {
                int32_t src_dims[3];
                int32_t src_level;
                int32_t dst_dims[3];
                int32_t dst_level;
                float falloff_factor;
                float energy_conserve;
                int32_t pad[2];
            } pc{};

            pc.src_dims[0] = static_cast<int32_t>(gi_cascades_[src_level].dims[0]);
            pc.src_dims[1] = static_cast<int32_t>(gi_cascades_[src_level].dims[1]);
            pc.src_dims[2] = static_cast<int32_t>(gi_cascades_[src_level].dims[2]);
            pc.src_level = static_cast<int32_t>(src_level);
            pc.dst_dims[0] = static_cast<int32_t>(gi_cascades_[dst_level].dims[0]);
            pc.dst_dims[1] = static_cast<int32_t>(gi_cascades_[dst_level].dims[1]);
            pc.dst_dims[2] = static_cast<int32_t>(gi_cascades_[dst_level].dims[2]);
            pc.dst_level = static_cast<int32_t>(dst_level);
            pc.falloff_factor = 0.9f;    /* Slight falloff per level */
            pc.energy_conserve = 0.95f;  /* Prevent energy explosion */

            vkCmdPushConstants(cmd, gi_propagate_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            /* Dispatch for destination dimensions */
            uint32_t dim = gi_cascades_[dst_level].dims[0];
            uint32_t groups = (dim + 3) / 4;
            vkCmdDispatch(cmd, groups, groups, groups);
        }

        /* Transition all cascades to SHADER_READ for sampling in lighting */
        VkImageMemoryBarrier final_barriers[GI_CASCADE_LEVELS];
        for (uint32_t level = 0; level < GI_CASCADE_LEVELS; level++)
        {
            final_barriers[level].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            final_barriers[level].pNext = nullptr;
            final_barriers[level].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            final_barriers[level].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            final_barriers[level].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            final_barriers[level].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            final_barriers[level].image = gi_cascades_[level].image;
            final_barriers[level].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            final_barriers[level].subresourceRange.baseMipLevel = 0;
            final_barriers[level].subresourceRange.levelCount = 1;
            final_barriers[level].subresourceRange.baseArrayLayer = 0;
            final_barriers[level].subresourceRange.layerCount = 1;
            final_barriers[level].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            final_barriers[level].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, GI_CASCADE_LEVELS, final_barriers);
    }

    void Renderer::update_deferred_gi_cascade_descriptors()
    {
        if (!gbuffer_initialized_ || !deferred_lighting_descriptor_pool_ || !gi_resources_initialized_)
            return;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorImageInfo cascade_infos[GI_CASCADE_LEVELS]{};
            for (uint32_t c = 0; c < GI_CASCADE_LEVELS; c++)
            {
                cascade_infos[c].sampler = gi_cascade_sampler_;
                cascade_infos[c].imageView = gi_cascades_[c].view;
                cascade_infos[c].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet writes[GI_CASCADE_LEVELS]{};
            for (uint32_t c = 0; c < GI_CASCADE_LEVELS; c++)
            {
                writes[c].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[c].dstSet = deferred_lighting_descriptor_sets_[i];
                writes[c].dstBinding = 9 + c; /* Bindings 9-12 */
                writes[c].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[c].descriptorCount = 1;
                writes[c].pImageInfo = &cascade_infos[c];
            }

            vkUpdateDescriptorSets(device_, GI_CASCADE_LEVELS, writes, 0, nullptr);
        }
    }

}
