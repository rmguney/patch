#include "voxel_objects.h"
#include "gpu_bvh.h"
#include "render_targets.h"
#include "vk_pipeline.h"
#include "voxel_push_constants.h"
#include "shaders_embedded.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    static_assert(sizeof(GPUBVHNode) == sizeof(BVHNode), "GPUBVHNode must mirror BVHNode");
    static_assert(GPU_BVH_MAX_NODES == BVH_MAX_NODES && GPU_BVH_MAX_OBJECTS == BVH_MAX_OBJECTS,
                  "GPU BVH capacity must match the CPU BVH");

    /* Sampled by the object fragment shader and the shadow trace */
    static constexpr VkPipelineStageFlags2 ATLAS_READERS =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    static void begin_one_time(VkCommandBuffer cmd)
    {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);
    }

    bool VoxelObjectAtlas::create(VulkanContext &ctx)
    {
        ctx_ = &ctx;

        ImageDesc desc;
        desc.type = VK_IMAGE_TYPE_3D;
        desc.format = VK_FORMAT_R8_UINT;
        desc.extent = {BRICK_ATLAS_SIZE, BRICK_ATLAS_SIZE, BRICK_ATLAS_SIZE};
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        const VkDeviceSize staging_size =
            static_cast<VkDeviceSize>(VOBJ_BRICK_VOXELS) * MAX_BRICK_UPLOADS_PER_FRAME * MAX_FRAMES_IN_FLIGHT;
        if (!ctx.create_image(desc, &image_) ||
            !create_point_sampler(ctx.device(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, &sampler_) ||
            !ctx.create_buffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging_))
        {
            fprintf(stderr, "Failed to create the voxel object atlas\n");
            return false;
        }

        /* Sampled (every brick, uploaded or not) before the first upload */
        VkCommandBuffer cmd = ctx.begin_immediate_commands();
        if (!cmd)
            return false;
        BarrierBatch barriers(cmd);
        barriers.transition(image_, image_use::sampled(ATLAS_READERS));
        barriers.record();
        if (!ctx.submit_immediate_commands(cmd))
            return false;

        regions_.resize(MAX_BRICK_UPLOADS_PER_FRAME);
        printf("  Voxel object atlas created: %ux%ux%u (%.1f MB, %u bricks of %d^3)\n", desc.extent.width,
               desc.extent.height, desc.extent.depth,
               static_cast<float>(desc.extent.width * desc.extent.height * desc.extent.depth) / (1024.0f * 1024.0f),
               BRICK_ATLAS_CAPACITY, VOBJ_BRICK_SIZE);
        return true;
    }

    void VoxelObjectAtlas::destroy()
    {
        if (!ctx_)
            return;
        ctx_->destroy_buffer(&staging_);
        destroy_sampler(ctx_->device(), &sampler_);
        ctx_->destroy_image(&image_);
    }

    static VkDeviceSize staging_frame_base(uint32_t slot)
    {
        return static_cast<VkDeviceSize>(slot) * VoxelObjectAtlas::MAX_BRICK_UPLOADS_PER_FRAME * VOBJ_BRICK_VOXELS;
    }

    uint8_t *VoxelObjectAtlas::staging(uint32_t slot) const
    {
        return static_cast<uint8_t *>(staging_.mapped) + staging_frame_base(slot);
    }

    void VoxelObjectAtlas::record_upload(VkCommandBuffer cmd, uint32_t slot, const uint16_t *atlas_bricks, uint32_t count)
    {
        const VkDeviceSize frame_base = staging_frame_base(slot);
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t origin[3];
            brick_atlas_origin(atlas_bricks[i], origin);
            VkBufferImageCopy &region = regions_[i];
            region = {};
            region.bufferOffset = frame_base + static_cast<VkDeviceSize>(i) * VOBJ_BRICK_VOXELS;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {static_cast<int32_t>(origin[0]), static_cast<int32_t>(origin[1]),
                                  static_cast<int32_t>(origin[2])};
            region.imageExtent = {VOBJ_BRICK_SIZE, VOBJ_BRICK_SIZE, VOBJ_BRICK_SIZE};
        }

        /* Only some bricks change: keep the rest, after earlier frames finished sampling them */
        BarrierBatch barriers(cmd);
        barriers.transition(image_, image_use::copy_dst);
        barriers.record();

        vkCmdCopyBufferToImage(cmd, staging_.buffer, image_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count,
                               regions_.data());

        barriers.transition(image_, image_use::sampled(ATLAS_READERS));
        barriers.record();
    }

    bool VoxelObjects::create(VulkanContext &ctx)
    {
        ctx_ = &ctx;
        VkDevice device = ctx.device();
        const VkShaderStageFlags fragment = VK_SHADER_STAGE_FRAGMENT_BIT;
        const VkShaderStageFlags vertex_fragment = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        if (!create_set_layout(device,
                               {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fragment},   /* atlas */
                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vertex_fragment},    /* metadata */
                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fragment},           /* material palette */
                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fragment}},          /* temporal UBO (motion vectors) */
                               &set_layout_) ||
            !create_pipeline_layout(device, {set_layout_}, vertex_fragment, sizeof(VoxelObjectPushConstants), &layout_))
        {
            fprintf(stderr, "Failed to create voxel object pipeline layout\n");
            return false;
        }

        GraphicsPipelineDesc desc{};
        desc.vertex = {shaders::k_shader_voxel_object_vert_spv, shaders::k_shader_voxel_object_vert_spv_size};
        desc.fragment = {shaders::k_shader_voxel_object_frag_spv, shaders::k_shader_voxel_object_frag_spv_size};
        desc.layout = layout_;
        desc.color_formats = RenderTargets::RASTER_COLOR_FORMATS;
        desc.color_count = RenderTargets::RASTER_COLOR_COUNT;
        desc.depth_format = RenderTargets::DEPTH_FORMAT;
        desc.depth_test = true;
        desc.depth_write = true;
        desc.depth_compare = VK_COMPARE_OP_LESS;
        desc.cull_mode = VK_CULL_MODE_BACK_BIT;
        GraphicsPipelineDesc inside_desc = desc;
        inside_desc.cull_mode = VK_CULL_MODE_FRONT_BIT;
        if (!create_graphics_pipeline(ctx, desc, &pipeline_) || !create_graphics_pipeline(ctx, inside_desc, &inside_pipeline_))
        {
            fprintf(stderr, "Failed to create voxel object pipeline\n");
            return false;
        }

        if (!create_descriptor_pool(device, MAX_FRAMES_IN_FLIGHT,
                                    {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * 2}},
                                    &pool_) ||
            !allocate_sets(device, pool_, set_layout_, MAX_FRAMES_IN_FLIGHT, sets_) ||
            !ctx.allocate_command_buffers(MAX_FRAMES_IN_FLIGHT, upload_cmd_))
        {
            fprintf(stderr, "Failed to allocate voxel object descriptor sets\n");
            return false;
        }
        return true;
    }

    void VoxelObjects::destroy()
    {
        if (!ctx_)
            return;
        destroy_scene();
        VkDevice device = ctx_->device();
        destroy_descriptor_pool(device, &pool_);
        destroy_pipeline(device, &pipeline_);
        destroy_pipeline(device, &inside_pipeline_);
        destroy_pipeline_layout(device, &layout_);
        destroy_set_layout(device, &set_layout_);
    }

    bool VoxelObjects::create_scene(uint32_t max_objects, const TerrainVolume &terrain, const FrameResources &frames)
    {
        if (terrain.material_palette() == VK_NULL_HANDLE)
        {
            fprintf(stderr, "Cannot create voxel object resources: the scene's terrain must be begun first\n");
            return false;
        }
        if (!atlas_.create(*ctx_))
            return false;
        capacity_ = max_objects < VOBJ_MAX_OBJECTS ? max_objects : VOBJ_MAX_OBJECTS;
        motion_.assign(capacity_, SlotMotion{});
        reset_tracking();

        /* Metadata and BVH are rewritten by the CPU every frame: one copy per frame in flight */
        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const VkDeviceSize metadata_size = static_cast<VkDeviceSize>(capacity_) * sizeof(VoxelObjectGPU);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (!ctx_->create_buffer(metadata_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, &metadata_[i]) ||
                !ctx_->create_buffer(sizeof(GPUBVHBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, &bvh_[i]))
                return false;
            memset(bvh_[i].mapped, 0, sizeof(GPUBVHBuffer));
        }

        cpu_bvh_ = bvh_create();
        if (!cpu_bvh_)
            return false;

        DescriptorWriter writer(ctx_->device());
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            writer.sampled(sets_[i], 0, atlas_.sampler(), atlas_.view())
                .buffer(sets_[i], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, metadata_[i].buffer)
                .buffer(sets_[i], 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, terrain.material_palette())
                .buffer(sets_[i], 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames.temporal_ubo[i].buffer,
                        sizeof(VoxelTemporalUBO));
        }
        writer.update();

        ready_ = true;
        return true;
    }

    void VoxelObjects::destroy_scene()
    {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            ctx_->destroy_buffer(&metadata_[i]);
            ctx_->destroy_buffer(&bvh_[i]);
        }
        atlas_.destroy();

        if (cpu_bvh_)
        {
            bvh_destroy(cpu_bvh_);
            cpu_bvh_ = nullptr;
        }

        total_count_ = 0;
        ready_ = false;
    }

    void VoxelObjects::reset_tracking()
    {
        /* The next update() starts over with every atlas brick free and no object motion */
        last_world_ = nullptr;
        total_count_ = 0;
        for (SlotMotion &motion : motion_)
            motion.valid = false;
    }

    bool VoxelObjects::update(const FrameContext &frame, const VoxelObjectWorld *world, float alpha,
                              const VoxelObjectPushConstants &pc)
    {
        if (!world || world->object_count == 0)
        {
            total_count_ = 0; /* no object shadows from stale metadata */
            return true;
        }

        if (world != last_world_)
        {
            reset_tracking();
            last_world_ = world;
            bricks_.reset(capacity_, BRICK_ATLAS_CAPACITY);
        }

        const uint32_t object_count = static_cast<uint32_t>(world->object_count);
        const uint32_t count = object_count < capacity_ ? object_count : capacity_;
        bricks_.sync(world, count);
        const bool uploaded = upload_changed_bricks(frame, world, count);
        write_metadata(frame.slot, world, count, alpha, pc);
        return uploaded;
    }

    void VoxelObjects::draw(const FrameContext &frame, VoxelObjectPushConstants pc) const
    {
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame.slot], 0, nullptr);

        /* One proxy cube (36 vertices) per metadata slot in each draw; the vertex shader collapses
         * hidden objects and those that belong to the other draw */
        const VkPipeline pipelines[2] = {pipeline_, inside_pipeline_};
        const int32_t draws = camera_near_ ? 2 : 1;
        for (int32_t inside = 0; inside < draws; inside++)
        {
            pc.camera_inside = inside;
            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[inside]);
            vkCmdPushConstants(frame.cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc), &pc);
            vkCmdDraw(frame.cmd, 36, static_cast<uint32_t>(total_count_), 0, 0);
        }
    }

    /* Uploads the bricks the world changed (new objects, destruction, splitting, recentering) */
    bool VoxelObjects::upload_changed_bricks(const FrameContext &frame, const VoxelObjectWorld *world, uint32_t count)
    {
        /* This slot's staging and previous upload belong to the slot's last frame, whose fence
         * begin_frame() has waited */
        const uint32_t staged = bricks_.stage(world, count, VoxelObjectAtlas::MAX_BRICK_UPLOADS_PER_FRAME,
                                              atlas_.staging(frame.slot), staged_bricks_);
        if (staged == 0)
            return true;

        VkCommandBuffer cmd = upload_cmd_[frame.slot];
        begin_one_time(cmd);
        atlas_.record_upload(cmd, frame.slot, staged_bricks_, staged);
        VkResult result = vkEndCommandBuffer(cmd);

        /* No fence: the frame's own fence covers this earlier submission */
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
            fprintf(stderr, "Voxel object brick upload failed: %s (%d)\n", vk_result_name(result), result);
            return false;
        }
        return true;
    }

    void VoxelObjects::write_metadata(uint32_t slot, const VoxelObjectWorld *world, uint32_t count, float alpha,
                                      const VoxelObjectPushConstants &pc)
    {
        /* Half extent of an object's proxy cube in voxels (voxel_object.vert) */
        const float half_cube = static_cast<float>(VOBJ_GRID_SIZE) * 0.5f * 1.1f;
        camera_near_ = false;

        VoxelObjectGPU *gpu_data = static_cast<VoxelObjectGPU *>(metadata_[slot].mapped);

        /*
         * Every object is written at its own index: the BVH and the instanced draw address
         * the metadata by object index. position.w = 0 hides an object (inactive, brick
         * upload still pending, or within its render delay).
         */
        for (uint32_t i = 0; i < count; i++)
        {
            const VoxelObject *obj = &world->objects[i];
            VoxelObjectGPU *gpu = &gpu_data[i];

            if (!obj->active)
            {
                memset(gpu, 0, sizeof(VoxelObjectGPU));
                motion_[i].valid = false;
                continue;
            }

            const float vs = obj->voxel_size;
            Vec3 translation; /* the object center; no center-of-mass offset */
            Quat orientation;
            voxel_object_draw_pose(obj, alpha, &translation, &orientation);
            draw_positions_[i] = translation;
            float rot_mat[9];
            quat_to_mat3(orientation, rot_mat);

            /* Column-major local_to_world = [R * voxel_size, pos] */
            const float world_mat[16] = {
                rot_mat[0] * vs, rot_mat[3] * vs, rot_mat[6] * vs, 0.0f,
                rot_mat[1] * vs, rot_mat[4] * vs, rot_mat[7] * vs, 0.0f,
                rot_mat[2] * vs, rot_mat[5] * vs, rot_mat[8] * vs, 0.0f,
                translation.x, translation.y, translation.z, 1.0f};

            /* Translation for world_to_local: t = -R^T * pos (row-major R; use columns of R) */
            const Vec3 local_origin = {
                -(translation.x * rot_mat[0] + translation.y * rot_mat[3] + translation.z * rot_mat[6]),
                -(translation.x * rot_mat[1] + translation.y * rot_mat[4] + translation.z * rot_mat[7]),
                -(translation.x * rot_mat[2] + translation.y * rot_mat[5] + translation.z * rot_mat[8])};

            /* Column-major world_to_local = [R^T, -R^T*pos] (no voxel_size scaling) */
            const float inv_mat[16] = {
                rot_mat[0], rot_mat[1], rot_mat[2], 0.0f,
                rot_mat[3], rot_mat[4], rot_mat[5], 0.0f,
                rot_mat[6], rot_mat[7], rot_mat[8], 0.0f,
                local_origin.x, local_origin.y, local_origin.z, 1.0f};

            memcpy(gpu->local_to_world, world_mat, sizeof(world_mat));
            memcpy(gpu->world_to_local, inv_mat, sizeof(inv_mat));

            /* Motion vectors: the transform the same object was drawn with last frame. A new
             * object in the slot, or one whose voxels changed (recentering moves its position
             * with them), starts without object motion. */
            SlotMotion &motion = motion_[i];
            const bool tracked = motion.valid && motion.alloc_id == obj->alloc_id &&
                                 motion.voxel_revision == obj->voxel_revision;
            memcpy(gpu->prev_local_to_world, tracked ? motion.local_to_world : world_mat, sizeof(world_mat));
            memcpy(motion.local_to_world, world_mat, sizeof(world_mat));
            motion.alloc_id = obj->alloc_id;
            motion.voxel_revision = obj->voxel_revision;
            motion.valid = true;

            /* The camera may be inside the proxy cube, or close enough for the near plane to cut
             * it: the cube's bounding sphere, grown like the vertex shader's per-axis test */
            const float reach = (half_cube * vs + pc.near_clip_radius) * 1.7320508f;
            const float dx = pc.camera_pos[0] - translation.x;
            const float dy = pc.camera_pos[1] - translation.y;
            const float dz = pc.camera_pos[2] - translation.z;
            if (dx * dx + dy * dy + dz * dz < reach * reach)
                camera_near_ = true;

            /* Local box of the voxels: about shape_center, which sits half a voxel off the
             * grid centre on axes with an odd voxel count */
            gpu->bounds_min[0] = obj->shape_center.x - obj->shape_half_extents.x;
            gpu->bounds_min[1] = obj->shape_center.y - obj->shape_half_extents.y;
            gpu->bounds_min[2] = obj->shape_center.z - obj->shape_half_extents.z;
            gpu->bounds_min[3] = vs;

            gpu->bounds_max[0] = obj->shape_center.x + obj->shape_half_extents.x;
            gpu->bounds_max[1] = obj->shape_center.y + obj->shape_half_extents.y;
            gpu->bounds_max[2] = obj->shape_center.z + obj->shape_half_extents.z;
            gpu->bounds_max[3] = static_cast<float>(VOBJ_GRID_SIZE);

            const bool visible = bricks_.drawable(i) && obj->render_delay <= 0;
            gpu->position[0] = translation.x;
            gpu->position[1] = translation.y;
            gpu->position[2] = translation.z;
            gpu->position[3] = visible ? 1.0f : 0.0f;

            const ObjectBricks::Slot &bricks = bricks_.slot(i);
            gpu->brick_mask[0] = static_cast<uint32_t>(bricks.resident);
            gpu->brick_mask[1] = static_cast<uint32_t>(bricks.resident >> 32);
            /* The offset the resident bricks were packed at: moving it restamps every brick,
             * so the object is hidden until all of them are uploaded again */
            gpu->brick_offset = static_cast<uint32_t>(obj->brick_offset[0]) |
                                (static_cast<uint32_t>(obj->brick_offset[1]) << 8) |
                                (static_cast<uint32_t>(obj->brick_offset[2]) << 16);
            gpu->pad = 0;
            memcpy(gpu->brick_table, bricks.table, sizeof(gpu->brick_table));
        }

        total_count_ = static_cast<int32_t>(count);

        /* Over the drawn poses: the shadow trace intersects the objects where they are drawn */
        if (bvh_needs_rebuild(cpu_bvh_, world))
            bvh_build(cpu_bvh_, world, draw_positions_);
        else
            bvh_refit(cpu_bvh_, world, draw_positions_);

        /* Only the live part of the node / index arrays is written */
        GPUBVHBuffer *gpu_bvh = static_cast<GPUBVHBuffer *>(bvh_[slot].mapped);
        gpu_bvh->params.node_count = cpu_bvh_->node_count;
        gpu_bvh->params.object_count = cpu_bvh_->object_count;
        memcpy(gpu_bvh->nodes, cpu_bvh_->nodes, sizeof(GPUBVHNode) * static_cast<size_t>(cpu_bvh_->node_count));
        memcpy(gpu_bvh->object_indices, cpu_bvh_->object_indices, sizeof(int32_t) * static_cast<size_t>(cpu_bvh_->object_count));
    }

}
