#include "renderer.h"
#include "renderer_internal.h"
#include <cstring>
#include <cstdio>

namespace patch
{

    Renderer::Renderer(Window &window)
        : window_(window), instance_(VK_NULL_HANDLE), physical_device_(VK_NULL_HANDLE), device_(VK_NULL_HANDLE), graphics_queue_(VK_NULL_HANDLE), present_queue_(VK_NULL_HANDLE), graphics_family_(UINT32_MAX), present_family_(UINT32_MAX), surface_(VK_NULL_HANDLE), swapchain_(VK_NULL_HANDLE), swapchain_format_(VK_FORMAT_UNDEFINED), swapchain_extent_{}, render_pass_(VK_NULL_HANDLE), pipeline_layout_(VK_NULL_HANDLE), ui_pipeline_(VK_NULL_HANDLE), camera_target_(vec3_zero()), prev_camera_target_(vec3_zero()), camera_initialized_(false), command_pool_(VK_NULL_HANDLE), current_frame_(0), depth_image_(VK_NULL_HANDLE), depth_image_memory_(VK_NULL_HANDLE), depth_image_view_(VK_NULL_HANDLE), depth_sampler_(VK_NULL_HANDLE), view_matrix_(mat4_identity()), projection_matrix_(mat4_identity()), prev_view_matrix_(mat4_identity()), prev_projection_matrix_(mat4_identity()), ortho_base_width_(0.0f), ortho_base_height_(0.0f), ortho_base_depth_(0.0f), ortho_half_width_(0.0f), ortho_half_height_(0.0f), projection_mode_(ProjectionMode::Orthographic), perspective_fov_y_degrees_(60.0f), perspective_near_(0.1f), perspective_far_(200.0f), camera_position_(vec3_zero()), init_error_(nullptr), material_count_(0), voxel_descriptor_layout_(VK_NULL_HANDLE), voxel_descriptor_pool_(VK_NULL_HANDLE), voxel_data_buffer_{VK_NULL_HANDLE, VK_NULL_HANDLE}, voxel_headers_buffer_{VK_NULL_HANDLE, VK_NULL_HANDLE}, voxel_material_buffer_{VK_NULL_HANDLE, VK_NULL_HANDLE}, voxel_total_chunks_(0), voxel_resources_initialized_(false), rt_supported_(false), rt_quality_(0), temporal_compute_pipeline_(VK_NULL_HANDLE), temporal_compute_layout_(VK_NULL_HANDLE), temporal_shadow_input_layout_(VK_NULL_HANDLE), temporal_shadow_output_layout_(VK_NULL_HANDLE), temporal_shadow_descriptor_pool_(VK_NULL_HANDLE), history_images_{VK_NULL_HANDLE, VK_NULL_HANDLE}, history_image_memory_{VK_NULL_HANDLE, VK_NULL_HANDLE}, history_image_views_{VK_NULL_HANDLE, VK_NULL_HANDLE}, history_write_index_(0), last_bound_pipeline_(VK_NULL_HANDLE), last_bound_descriptor_set_(VK_NULL_HANDLE), gbuffer_sampler_(VK_NULL_HANDLE), gbuffer_render_pass_(VK_NULL_HANDLE), gbuffer_framebuffer_(VK_NULL_HANDLE), gbuffer_descriptor_layout_(VK_NULL_HANDLE), gbuffer_descriptor_pool_(VK_NULL_HANDLE), gbuffer_pipeline_(VK_NULL_HANDLE), gbuffer_pipeline_layout_(VK_NULL_HANDLE), deferred_lighting_pipeline_(VK_NULL_HANDLE), deferred_lighting_layout_(VK_NULL_HANDLE), deferred_lighting_descriptor_layout_(VK_NULL_HANDLE), deferred_lighting_descriptor_pool_(VK_NULL_HANDLE), shadow_volume_image_(VK_NULL_HANDLE), shadow_volume_memory_(VK_NULL_HANDLE), shadow_volume_view_(VK_NULL_HANDLE), shadow_volume_sampler_(VK_NULL_HANDLE), blue_noise_image_(VK_NULL_HANDLE), blue_noise_memory_(VK_NULL_HANDLE), blue_noise_view_(VK_NULL_HANDLE), blue_noise_sampler_(VK_NULL_HANDLE), motion_vector_image_(VK_NULL_HANDLE), motion_vector_memory_(VK_NULL_HANDLE), motion_vector_view_(VK_NULL_HANDLE), gbuffer_initialized_(false), timestamp_query_pool_(VK_NULL_HANDLE), timestamp_period_ns_(0.0f), timestamps_supported_(false)
    {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            lighting_ubo_[i].buffer = VK_NULL_HANDLE;
            lighting_ubo_[i].memory = VK_NULL_HANDLE;
            voxel_descriptor_sets_[i] = VK_NULL_HANDLE;
            voxel_temporal_ubo_[i].buffer = VK_NULL_HANDLE;
            voxel_temporal_ubo_[i].memory = VK_NULL_HANDLE;
            gbuffer_descriptor_sets_[i] = VK_NULL_HANDLE;
            deferred_lighting_descriptor_sets_[i] = VK_NULL_HANDLE;
            temporal_shadow_input_sets_[i] = VK_NULL_HANDLE;
            temporal_shadow_output_sets_[i] = VK_NULL_HANDLE;
        }
        for (uint32_t i = 0; i < GBUFFER_COUNT; i++)
        {
            gbuffer_images_[i] = VK_NULL_HANDLE;
            gbuffer_memory_[i] = VK_NULL_HANDLE;
            gbuffer_views_[i] = VK_NULL_HANDLE;
        }
        shadow_volume_dims_[0] = shadow_volume_dims_[1] = shadow_volume_dims_[2] = 0;
        shadow_volume_last_frame_ = 0;
        memset(material_palette_, 0, sizeof(material_palette_));
        memset(material_entries_, 0, sizeof(material_entries_));
        use_full_materials_ = false;
    }

    Renderer::~Renderer()
    {
        cleanup();
    }

    void Renderer::set_material_palette(const Vec3 *colors, int32_t count)
    {
        material_count_ = count < VOXEL_MATERIAL_MAX ? count : VOXEL_MATERIAL_MAX;
        memcpy(material_palette_, colors, material_count_ * sizeof(Vec3));
        use_full_materials_ = false;
    }

    void Renderer::set_material_palette_full(const MaterialEntry *materials, int32_t count)
    {
        material_count_ = count < VOXEL_MATERIAL_MAX ? count : VOXEL_MATERIAL_MAX;
        memcpy(material_entries_, materials, material_count_ * sizeof(MaterialEntry));
        for (int32_t i = 0; i < material_count_; i++)
        {
            material_palette_[i].x = materials[i].r;
            material_palette_[i].y = materials[i].g;
            material_palette_[i].z = materials[i].b;
        }
        use_full_materials_ = true;
    }

    void Renderer::set_rt_quality(int level)
    {
        rt_quality_ = level < 0 ? 0 : (level > 3 ? 3 : level);
    }

    void Renderer::set_shadow_quality(int level)
    {
        shadow_quality_ = level < 0 ? 0 : (level > 3 ? 3 : level);
    }

    void Renderer::set_shadow_contact_hardening(bool enabled)
    {
        shadow_contact_hardening_ = enabled;
    }

    void Renderer::set_ao_quality(int level)
    {
        ao_quality_ = level < 0 ? 0 : (level > 2 ? 2 : level);
    }

    void Renderer::set_lod_quality(int level)
    {
        lod_quality_ = level < 0 ? 0 : (level > 2 ? 2 : level);
    }

    void Renderer::set_reflection_quality(int level)
    {
        reflection_quality_ = level < 0 ? 0 : (level > 2 ? 2 : level);
    }

    void Renderer::set_adaptive_quality(bool enabled)
    {
        adaptive_quality_ = enabled;
        if (enabled && rt_quality_ < 1)
            rt_quality_ = 1; /* Minimum quality 1 when adaptive is on */
        adaptive_cooldown_ = 0;
    }

    void Renderer::update_adaptive_quality(float frame_time_ms)
    {
        if (!adaptive_quality_)
            return;
        if (adaptive_cooldown_ > 0)
        {
            adaptive_cooldown_--;
            return;
        }

        constexpr float HIGH_MS = 20.0f;

        if (frame_time_ms > HIGH_MS && rt_quality_ > 1)
        {
            rt_quality_--;
            shadow_quality_ = rt_quality_ + 1;
            ao_quality_ = rt_quality_ > 0 ? rt_quality_ : 1;
            adaptive_cooldown_ = ADAPTIVE_COOLDOWN_FRAMES;
        }
    }

    bool Renderer::init()
    {
        gpu_name_[0] = '\0';

        if (!create_instance())
        {
            init_error_ = "Failed to create Vulkan instance";
            return false;
        }

        surface_ = window_.create_surface(instance_);
        if (surface_ == VK_NULL_HANDLE)
        {
            init_error_ = "Failed to create Vulkan surface";
            return false;
        }

        if (!select_physical_device())
        {
            init_error_ = "Failed to select physical device";
            return false;
        }

        if (!find_queue_families())
        {
            init_error_ = "Failed to find queue families";
            return false;
        }

        if (!create_logical_device())
        {
            init_error_ = "Failed to create logical device";
            return false;
        }

        if (!create_swapchain())
        {
            init_error_ = "Failed to create swapchain";
            return false;
        }

        if (!create_render_pass())
        {
            init_error_ = "Failed to create render pass";
            return false;
        }

        if (!create_depth_resources())
        {
            init_error_ = "Failed to create depth resources";
            return false;
        }

        if (!create_pipelines())
        {
            init_error_ = "Failed to create pipelines";
            return false;
        }

        VkDeviceSize ubo_size = sizeof(ShadowUniforms);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            create_buffer(ubo_size,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &lighting_ubo_[i]);
        }

        if (!create_voxel_descriptor_layout())
        {
            init_error_ = "Failed to create voxel descriptor layout";
            return false;
        }

        if (!create_framebuffers())
        {
            init_error_ = "Failed to create framebuffers";
            return false;
        }

        if (!create_command_pool())
        {
            init_error_ = "Failed to create command pool";
            return false;
        }

        if (!create_sync_objects())
        {
            init_error_ = "Failed to create sync objects";
            return false;
        }

        if (!create_timestamp_query_pool())
        {
            init_error_ = "Failed to create timestamp query pool";
            return false;
        }

        create_quad_mesh();
        total_frame_count_ = 0;

        if (!init_deferred_pipeline())
        {
            init_error_ = "Failed to initialize deferred rendering pipeline";
            return false;
        }

        float iso_distance = 30.0f;
        float iso_yaw = 45.0f * K_DEG_TO_RAD;
        float iso_pitch = 35.26f * K_DEG_TO_RAD;

        Vec3 eye = vec3_create(
            iso_distance * sinf(iso_yaw) * cosf(iso_pitch),
            iso_distance * sinf(iso_pitch),
            iso_distance * cosf(iso_yaw) * cosf(iso_pitch));

        view_matrix_ = mat4_look_at(eye, vec3_zero(), vec3_create(0.0f, 1.0f, 0.0f));

        float aspect = window_.aspect_ratio();
        float ortho_size = 10.0f;
        projection_matrix_ = mat4_ortho(-ortho_size * aspect, ortho_size * aspect, -ortho_size, ortho_size, 0.1f, 100.0f);
        ortho_half_width_ = ortho_size * aspect;
        ortho_half_height_ = ortho_size;

        return true;
    }

    void Renderer::cleanup()
    {
        if (device_ != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(device_);

            destroy_buffer(&quad_mesh_.vertex);
            destroy_buffer(&quad_mesh_.index);

            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                destroy_buffer(&lighting_ubo_[i]);
            }

            destroy_buffer(&voxel_data_buffer_);
            destroy_buffer(&voxel_headers_buffer_);
            destroy_buffer(&voxel_material_buffer_);
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                destroy_buffer(&voxel_temporal_ubo_[i]);
            }

            /* Unmap and destroy staging buffers */
            if (staging_voxels_mapped_)
            {
                vkUnmapMemory(device_, staging_voxels_buffer_.memory);
                staging_voxels_mapped_ = nullptr;
            }
            if (staging_headers_mapped_)
            {
                vkUnmapMemory(device_, staging_headers_buffer_.memory);
                staging_headers_mapped_ = nullptr;
            }
            destroy_buffer(&staging_voxels_buffer_);
            destroy_buffer(&staging_headers_buffer_);

            if (voxel_descriptor_pool_)
                vkDestroyDescriptorPool(device_, voxel_descriptor_pool_, nullptr);
            if (voxel_descriptor_layout_)
                vkDestroyDescriptorSetLayout(device_, voxel_descriptor_layout_, nullptr);

            if (temporal_compute_pipeline_)
                vkDestroyPipeline(device_, temporal_compute_pipeline_, nullptr);
            if (temporal_compute_layout_)
                vkDestroyPipelineLayout(device_, temporal_compute_layout_, nullptr);
            if (temporal_shadow_descriptor_pool_)
                vkDestroyDescriptorPool(device_, temporal_shadow_descriptor_pool_, nullptr);
            if (temporal_shadow_input_layout_)
                vkDestroyDescriptorSetLayout(device_, temporal_shadow_input_layout_, nullptr);
            if (temporal_shadow_output_layout_)
                vkDestroyDescriptorSetLayout(device_, temporal_shadow_output_layout_, nullptr);

            destroy_timestamp_query_pool();
            destroy_gbuffer_resources();
            destroy_shadow_volume_resources();
            destroy_blue_noise_texture();
            destroy_motion_vector_resources();
            destroy_particle_resources();

            for (int i = 0; i < 2; i++)
            {
                if (history_image_views_[i])
                    vkDestroyImageView(device_, history_image_views_[i], nullptr);
                if (history_images_[i])
                    vkDestroyImage(device_, history_images_[i], nullptr);
                if (history_image_memory_[i])
                    vkFreeMemory(device_, history_image_memory_[i], nullptr);
            }

            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                if (image_available_semaphores_[i])
                    vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
                if (render_finished_semaphores_[i])
                    vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
                if (in_flight_fences_[i])
                    vkDestroyFence(device_, in_flight_fences_[i], nullptr);
            }

            if (upload_timeline_semaphore_)
                vkDestroySemaphore(device_, upload_timeline_semaphore_, nullptr);
            for (uint32_t i = 0; i < pending_destroy_count_; i++)
            {
                destroy_buffer(&pending_destroys_[i].buffer);
            }
            pending_destroy_count_ = 0;

            if (command_pool_)
                vkDestroyCommandPool(device_, command_pool_, nullptr);

            for (auto fb : framebuffers_)
            {
                vkDestroyFramebuffer(device_, fb, nullptr);
            }

            if (depth_image_view_)
                vkDestroyImageView(device_, depth_image_view_, nullptr);
            if (depth_image_)
                vkDestroyImage(device_, depth_image_, nullptr);
            if (depth_image_memory_)
                vkFreeMemory(device_, depth_image_memory_, nullptr);
            if (depth_sampler_)
                vkDestroySampler(device_, depth_sampler_, nullptr);

            if (ui_pipeline_)
                vkDestroyPipeline(device_, ui_pipeline_, nullptr);

            if (pipeline_layout_)
                vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            if (render_pass_)
                vkDestroyRenderPass(device_, render_pass_, nullptr);

            for (auto view : swapchain_image_views_)
            {
                vkDestroyImageView(device_, view, nullptr);
            }

            if (swapchain_)
                vkDestroySwapchainKHR(device_, swapchain_, nullptr);

            vkDestroyDevice(device_, nullptr);
        }

        if (surface_ && instance_)
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_)
            vkDestroyInstance(instance_, nullptr);
    }

    void Renderer::begin_frame(uint32_t *image_index)
    {
        total_frame_count_++;

        Mat4 view_proj = mat4_multiply(projection_matrix_, view_matrix_);
        frustum_ = frustum_from_view_proj(view_proj);
        camera_forward_ = vec3_create(-view_matrix_.m[2], -view_matrix_.m[6], -view_matrix_.m[10]);

        vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_semaphores_[current_frame_], VK_NULL_HANDLE, image_index);
        vkResetCommandBuffer(command_buffers_[current_frame_], 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(command_buffers_[current_frame_], &begin_info);
        reset_bind_state();

        if (timestamps_supported_)
        {
            uint32_t query_offset = current_frame_ * GPU_TIMESTAMP_COUNT;
            vkCmdResetQueryPool(command_buffers_[current_frame_], timestamp_query_pool_,
                                query_offset, GPU_TIMESTAMP_COUNT);
        }
    }

    void Renderer::reset_bind_state()
    {
        last_bound_pipeline_ = VK_NULL_HANDLE;
        last_bound_descriptor_set_ = VK_NULL_HANDLE;
    }

    void Renderer::bind_pipeline(VkPipeline pipeline)
    {
        if (pipeline != last_bound_pipeline_)
        {
            vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            last_bound_pipeline_ = pipeline;
        }
    }

    void Renderer::bind_descriptor_set(VkDescriptorSet set)
    {
        if (set != last_bound_descriptor_set_)
        {
            vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout_, 0, 1, &set, 0, nullptr);
            last_bound_descriptor_set_ = set;
        }
    }

    void Renderer::begin_main_pass(uint32_t image_index)
    {
        if (timestamps_supported_)
        {
            uint32_t query_offset = current_frame_ * GPU_TIMESTAMP_COUNT;
            vkCmdWriteTimestamp(command_buffers_[current_frame_],
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                timestamp_query_pool_, query_offset + 2);
        }

        VkClearValue clear_values[2];
        clear_values[0].color = {{0.85f, 0.93f, 1.0f, 1.0f}}; /* Light pastel baby blue */
        clear_values[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = render_pass_;
        render_pass_info.framebuffer = framebuffers_[image_index];
        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = swapchain_extent_;
        render_pass_info.clearValueCount = 2;
        render_pass_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(command_buffers_[current_frame_], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
        cmd_set_viewport_scissor(command_buffers_[current_frame_], swapchain_extent_);
        reset_bind_state();
    }

    void Renderer::end_frame(uint32_t image_index)
    {
        vkCmdEndRenderPass(command_buffers_[current_frame_]);

        if (timestamps_supported_)
        {
            uint32_t query_offset = current_frame_ * GPU_TIMESTAMP_COUNT;
            vkCmdWriteTimestamp(command_buffers_[current_frame_],
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                timestamp_query_pool_, query_offset + 3);
        }

        vkEndCommandBuffer(command_buffers_[current_frame_]);

        VkSemaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers_[current_frame_];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_]);

        VkSwapchainKHR swapchains[] = {swapchain_};

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;

        vkQueuePresentKHR(present_queue_, &present_info);

        /* Save current matrices for next frame's temporal reprojection */
        prev_view_matrix_ = view_matrix_;
        prev_projection_matrix_ = projection_matrix_;

        current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::set_orthographic(float width, float height, float depth)
    {
        projection_mode_ = ProjectionMode::Orthographic;
        ortho_base_width_ = width;
        ortho_base_height_ = height;
        ortho_base_depth_ = depth;
        float aspect = window_.aspect_ratio();
        float half_width = width * aspect * 0.5f;
        float half_height = height * 0.5f;
        projection_matrix_ = mat4_ortho(-half_width, half_width, -half_height, half_height, 0.1f, depth);
        ortho_half_width_ = half_width;
        ortho_half_height_ = half_height;
    }

    void Renderer::update_perspective_projection()
    {
        float aspect = window_.aspect_ratio();
        if (aspect < 0.01f)
            aspect = 1.0f;
        projection_matrix_ = mat4_perspective(perspective_fov_y_degrees_ * K_DEG_TO_RAD, aspect, perspective_near_, perspective_far_);
    }

    void Renderer::set_perspective(float fov_y_degrees, float near_val, float far_val)
    {
        projection_mode_ = ProjectionMode::Perspective;
        perspective_fov_y_degrees_ = fov_y_degrees;
        perspective_near_ = near_val;
        perspective_far_ = far_val;
        update_perspective_projection();
    }

    void Renderer::on_resize()
    {
        recreate_swapchain();
        if (ortho_base_width_ > 0.0f && ortho_base_height_ > 0.0f && ortho_base_depth_ > 0.0f)
        {
            set_orthographic(ortho_base_width_, ortho_base_height_, ortho_base_depth_);
        }
        if (projection_mode_ == ProjectionMode::Perspective)
        {
            update_perspective_projection();
        }
    }

    void Renderer::set_view_angle(float yaw_degrees, float distance)
    {
        float yaw = yaw_degrees * K_DEG_TO_RAD;
        float tilt = 35.26f * K_DEG_TO_RAD;

        float cos_yaw = cosf(yaw);
        float sin_yaw = sinf(yaw);
        float cos_tilt = cosf(tilt);
        float sin_tilt = sinf(tilt);

        camera_position_ = vec3_create(
            distance * sin_yaw * cos_tilt,
            distance * sin_tilt,
            distance * cos_yaw * cos_tilt);

        camera_target_ = vec3_zero();
        camera_initialized_ = true;
        view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
    }

    void Renderer::set_view_angle_at(float yaw_degrees, float distance, Vec3 target)
    {
        float yaw = yaw_degrees * K_DEG_TO_RAD;
        float tilt = 35.26f * K_DEG_TO_RAD;

        float cos_yaw = cosf(yaw);
        float sin_yaw = sinf(yaw);
        float cos_tilt = cosf(tilt);
        float sin_tilt = sinf(tilt);

        Vec3 offset = vec3_create(
            distance * sin_yaw * cos_tilt,
            distance * sin_tilt,
            distance * cos_yaw * cos_tilt);

        camera_position_ = vec3_add(target, offset);
        camera_target_ = target;
        camera_initialized_ = true;
        view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
    }

    void Renderer::set_view_angle_at(float yaw_degrees, float distance, Vec3 target, float dt)
    {
        float yaw = yaw_degrees * K_DEG_TO_RAD;
        float tilt = 35.26f * K_DEG_TO_RAD;

        float cos_yaw = cosf(yaw);
        float sin_yaw = sinf(yaw);
        float cos_tilt = cosf(tilt);
        float sin_tilt = sinf(tilt);

        Vec3 offset = vec3_create(
            distance * sin_yaw * cos_tilt,
            distance * sin_tilt,
            distance * cos_yaw * cos_tilt);

        Vec3 desired_position = vec3_add(target, offset);
        Vec3 desired_target = target;

        if (!camera_initialized_ || dt <= 0.0f || dt > 0.25f)
        {
            camera_position_ = desired_position;
            camera_target_ = desired_target;
            camera_initialized_ = true;
            view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
            return;
        }

        constexpr float smooth_time = 0.08f;
        float alpha = 1.0f - expf(-dt / smooth_time);
        alpha = clampf(alpha, 0.0f, 1.0f);

        Vec3 pos_delta = vec3_sub(desired_position, camera_position_);
        Vec3 tgt_delta = vec3_sub(desired_target, camera_target_);
        camera_position_ = vec3_add(camera_position_, vec3_scale(pos_delta, alpha));
        camera_target_ = vec3_add(camera_target_, vec3_scale(tgt_delta, alpha));

        view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
    }

    void Renderer::set_look_at(Vec3 eye, Vec3 target)
    {
        camera_position_ = eye;
        camera_target_ = target;
        camera_initialized_ = true;
        view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
    }

    void Renderer::set_look_at(Vec3 eye, Vec3 target, float dt)
    {
        if (!camera_initialized_ || dt <= 0.0f || dt > 0.25f)
        {
            set_look_at(eye, target);
            return;
        }

        constexpr float smooth_time = 0.10f;
        float alpha = 1.0f - expf(-dt / smooth_time);
        alpha = clampf(alpha, 0.0f, 1.0f);

        Vec3 pos_delta = vec3_sub(eye, camera_position_);
        Vec3 tgt_delta = vec3_sub(target, camera_target_);
        camera_position_ = vec3_add(camera_position_, vec3_scale(pos_delta, alpha));
        camera_target_ = vec3_add(camera_target_, vec3_scale(tgt_delta, alpha));

        view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
    }

    bool Renderer::screen_to_world_floor(float screen_x, float screen_y, float floor_y, Vec3 *out_world) const
    {
        if (!out_world)
            return false;

        Vec3 origin_world, dir_world;
        screen_to_ray(screen_x, screen_y, &origin_world, &dir_world);

        float denom = dir_world.y;
        if (fabsf(denom) < 1e-5f)
        {
            return false;
        }

        float t = (floor_y - origin_world.y) / denom;
        *out_world = vec3_add(origin_world, vec3_scale(dir_world, t));
        return true;
    }

    void Renderer::screen_to_ray(float screen_x, float screen_y, Vec3 *out_origin, Vec3 *out_dir) const
    {
        float w = static_cast<float>(window_.width());
        float h = static_cast<float>(window_.height());
        if (w < 1.0f)
            w = 1.0f;
        if (h < 1.0f)
            h = 1.0f;

        float nx = (2.0f * screen_x / w) - 1.0f;
        float ny = 1.0f - (2.0f * screen_y / h);

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);

        if (projection_mode_ == ProjectionMode::Perspective)
        {
            float aspect = window_.aspect_ratio();
            if (aspect < 0.01f)
                aspect = 1.0f;
            float tan_half = tanf((perspective_fov_y_degrees_ * K_DEG_TO_RAD) * 0.5f);

            Vec3 dir_view = vec3_create(nx * aspect * tan_half, ny * tan_half, -1.0f);
            dir_view = vec3_normalize(dir_view);

            if (out_origin)
                *out_origin = camera_position_;
            if (out_dir)
                *out_dir = vec3_normalize(mat4_transform_direction(inv_view, dir_view));
            return;
        }

        Vec3 origin_view = vec3_create(nx * ortho_half_width_, ny * ortho_half_height_, 0.0f);
        Vec3 dir_view = vec3_create(0.0f, 0.0f, -1.0f);

        if (out_origin)
            *out_origin = mat4_transform_point(inv_view, origin_view);
        if (out_dir)
            *out_dir = vec3_normalize(mat4_transform_direction(inv_view, dir_view));
    }

}
