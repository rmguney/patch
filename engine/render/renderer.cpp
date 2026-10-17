#include "renderer.h"
#include "engine/core/profile.h"
#include "engine/platform/platform.h"
#include "engine/platform/thread.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace patch
{

    static float ticks_to_ms(int64_t start, int64_t end)
    {
        const int64_t freq = platform_get_frequency();
        return freq > 0 ? (float)(end - start) * 1000.0f / (float)freq : 0.0f;
    }

    /* Frames of the TAA jitter's cycle: its Halton (2, 3) points cover a pixel evenly within
     * the history's reach (taa_resolve.frag) */
    static constexpr uint32_t TAA_JITTER_PHASES = 8;

    /* Element index (from 1) of the Halton sequence of base, in [0, 1) */
    static float halton(uint32_t index, uint32_t base)
    {
        float scale = 1.0f;
        float result = 0.0f;
        for (; index > 0; index /= base)
        {
            scale /= static_cast<float>(base);
            result += scale * static_cast<float>(index % base);
        }
        return result;
    }

    /* All members carry default initializers in renderer.h */
    Renderer::Renderer(Window &window)
        : window_(window)
    {
    }

    Renderer::~Renderer()
    {
        cleanup();
    }

    bool Renderer::init()
    {
        if (!ctx_.create(window_, &init_error_))
            return false;

        if (!swapchain_.create(ctx_, window_))
        {
            init_error_ = "Failed to create swapchain";
            return false;
        }

        if (!frames_.create(ctx_))
        {
            init_error_ = "Failed to create frame resources";
            return false;
        }

        {
            SceneLighting defaults = scene_lighting_default();
            set_scene_lighting(&defaults);
        }

        timer_.create(ctx_);

        if (!ui_.create(ctx_, swapchain_.format()))
        {
            init_error_ = "Failed to create UI pipeline";
            return false;
        }

        printf("Initializing deferred rendering pipeline...\n");
        if (!targets_.create(ctx_, swapchain_.extent(), swapchain_.format()) ||
            !gbuffer_.create(ctx_) ||
            !shadow_ao_.create(ctx_, frames_) ||
            !lighting_.create(ctx_, frames_, swapchain_.format()) ||
            !post_.create(ctx_, swapchain_.format()) ||
            !objects_.create(ctx_) ||
            !particles_.create(ctx_, frames_) ||
            !terrain_.create(ctx_))
        {
            init_error_ = "Failed to initialize deferred rendering pipeline";
            return false;
        }
        bind_targets();
        printf("  Deferred pipeline initialized: %ux%u\n", targets_.extent.width, targets_.extent.height);
        return true;
    }

    /* Also runs after a failed init(): every subsystem checks the handles it releases */
    void Renderer::cleanup()
    {
        abandon_scene_load();
        if (ctx_.device() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(ctx_.device());

            objects_.destroy();
            terrain_.destroy();
            particles_.destroy();
            post_.destroy();
            lighting_.destroy();
            shadow_ao_.destroy();
            gbuffer_.destroy();
            targets_.destroy(ctx_);
            ui_.destroy();
            timer_.destroy();
            frames_.destroy(ctx_);
            swapchain_.destroy();
        }
        ctx_.destroy();
    }

    void Renderer::bind_targets()
    {
        gbuffer_.bind_targets(targets_);
        shadow_ao_.bind_targets(targets_);
        lighting_.bind_targets(targets_);
        post_.bind_targets(targets_);
    }

    /* Releases every scene-dependent resource; the next scene starts from a clean slate.
     * Each destroy_scene() checks handles, so this is also safe on partially created state. */
    void Renderer::prepare_for_scene_switch()
    {
        abandon_scene_load();
        vkDeviceWaitIdle(ctx_.device());

        objects_.destroy_scene();
        shadow_ao_.unbind_objects();
        terrain_.destroy_scene();

        reset_scene_state();
    }

    /* Runs between scenes, when no frame in flight uses the lighting sets it rewrites */
    void Renderer::reset_scene_state()
    {
        /* The lighting samples the raw shadow / AO outputs until the first resolves */
        reset_temporal_history();
        lighting_.bind_targets(targets_);
        objects_.reset_tracking();
        recent_frame_ms_ = 0.0f;
        particle_time_valid_ = false;
    }

    /* History from another scene or another resolution must not be reprojected into the next
     * frame: the resolves and TAA start over, and its motion vectors see no camera motion */
    void Renderer::reset_temporal_history()
    {
        shadow_ao_.reset_history();
        post_.reset_history();
        prev_view_matrix_ = view_matrix_;
        prev_projection_matrix_ = projection_matrix_;
    }

    /*
     * Every rebuild happens here, between frames: wait for the frames in flight, retire the
     * swapchain into one at the surface's size and, when that size changed, rebuild everything
     * sized to it. Pipelines, layouts and descriptor sets stay; the descriptor writes that
     * reference a target are redone, and the temporal history starts over at the new size.
     */
    bool Renderer::rebuild_swapchain()
    {
        const int64_t start = platform_get_ticks();
        vkDeviceWaitIdle(ctx_.device());
        const int64_t idle = platform_get_ticks();

        if (!swapchain_.rebuild())
        {
            if (swapchain_.failed())
                quit_requested_ = true;
            return false;
        }
        const int64_t rebuilt = platform_get_ticks();

        const VkExtent2D extent = swapchain_.extent();
        const bool resized = extent.width != targets_.extent.width || extent.height != targets_.extent.height;
        if (resized)
        {
            targets_.destroy(ctx_);
            if (!targets_.create(ctx_, extent, swapchain_.format()))
            {
                fprintf(stderr, "Renderer: creating the %ux%u render targets failed, shutting down\n",
                        extent.width, extent.height);
                quit_requested_ = true;
                return false;
            }
            bind_targets();
            update_perspective_projection();
            reset_temporal_history();
        }

        printf("  Swapchain rebuilt: %ux%u, %u images (GPU idle %.2f ms, swapchain %.2f ms, render targets %.2f ms)\n",
               extent.width, extent.height, swapchain_.image_count(), ticks_to_ms(start, idle),
               ticks_to_ms(idle, rebuilt), resized ? ticks_to_ms(rebuilt, platform_get_ticks()) : 0.0f);
        return true;
    }

    /* The terrain's CPU stages, on the load's thread. The weights are about their milliseconds
     * on a 32-thread machine. */
    static const LoadingStage TERRAIN_LOAD_STAGES[] = {
        {"Packing the shadow volume", 60.0f},
        {"Staging the terrain", 30.0f},
    };
    static constexpr int32_t TERRAIN_LOAD_STAGE_COUNT = static_cast<int32_t>(sizeof(TERRAIN_LOAD_STAGES) / sizeof(TERRAIN_LOAD_STAGES[0]));
    static_assert(TERRAIN_LOAD_STAGE_COUNT == Renderer::SCENE_LOAD_STAGES, "Renderer::SCENE_LOAD_STAGES counts the stages");

    void Renderer::run_scene_load_stage(void *renderer, int32_t stage)
    {
        Renderer *self = static_cast<Renderer *>(renderer);
        if (stage == 0)
            self->terrain_.pack_shadow_volume(self->scene_load_jobs_);
        else
            self->terrain_.stage_chunks(self->scene_load_jobs_);
    }

    void Renderer::begin_scene_load(VoxelVolume *vol, uint32_t max_objects, LoadingState *state)
    {
        /* Whatever the last scene left, a loaded scene or one abandoned half way */
        prepare_for_scene_switch();
        loading_state_init(state, TERRAIN_LOAD_STAGE_COUNT);

        PROFILE_BEGIN(PROFILE_VOLUME_INIT);
        const bool begun = terrain_.begin_scene(vol);
        PROFILE_END(PROFILE_VOLUME_INIT);
        if (!begun)
        {
            fprintf(stderr, "Renderer: creating the scene's GPU resources failed, shutting down\n");
            quit_requested_ = true;
            return;
        }

        /* Behind the loading screen too, not at the scene's first frame */
        if (!objects_.create_scene(max_objects, terrain_, frames_))
        {
            fprintf(stderr, "Renderer: creating the voxel object resources failed, shutting down\n");
            quit_requested_ = true;
            return;
        }
        shadow_ao_.bind_objects(objects_);
        printf("  Voxel object resources initialized: %u max objects\n", objects_.capacity());

        /* Every hardware thread but the presenting one; without a thread to load on, the
         * stages run here and the screen stands still meanwhile */
        const int32_t hardware = platform_thread_hardware_count();
        scene_load_jobs_ = job_system_create(hardware > 1 ? hardware - 1 : 1);
        scene_load_ = loading_start(TERRAIN_LOAD_STAGES, TERRAIN_LOAD_STAGE_COUNT, run_scene_load_stage, this);
        if (!scene_load_)
        {
            for (int32_t stage = 0; stage < TERRAIN_LOAD_STAGE_COUNT; stage++)
            {
                loading_state_advance(state, TERRAIN_LOAD_STAGES[stage].name);
                loading_log_stage(stage, TERRAIN_LOAD_STAGE_COUNT, TERRAIN_LOAD_STAGES[stage].name);
                run_scene_load_stage(this, stage);
            }
        }
    }

    bool Renderer::poll_scene_load(LoadingState *state)
    {
        if (quit_requested_)
            return true;

        if (!scene_uploading_)
        {
            if (scene_load_)
            {
                /* A loading screen frame every 16 ms while the stages run */
                if (!loading_poll(scene_load_, state, 16))
                    return false;
                loading_finish(scene_load_);
                scene_load_ = nullptr;
            }
            job_system_destroy(scene_load_jobs_);
            scene_load_jobs_ = nullptr;

            state->stage_name = "Uploading the terrain";
            state->progress = 1.0f;
            scene_uploading_ = true;
        }

        switch (terrain_.poll_scene())
        {
        case TerrainVolume::SceneUpload::Pending:
            return false;
        case TerrainVolume::SceneUpload::Failed:
            quit_requested_ = true;
            return true;
        case TerrainVolume::SceneUpload::Done:
            break;
        }
        scene_uploading_ = false;
        loading_state_complete(state);
        gbuffer_.bind_scene(terrain_, frames_);
        shadow_ao_.bind_scene(terrain_);
        lighting_.bind_scene(terrain_);
        return true;
    }

    void Renderer::abandon_scene_load()
    {
        loading_finish(scene_load_);
        scene_load_ = nullptr;
        job_system_destroy(scene_load_jobs_);
        scene_load_jobs_ = nullptr;
        scene_uploading_ = false;
    }

    void Renderer::set_material_palette(const MaterialEntry *materials, int32_t count)
    {
        terrain_.set_material_palette(materials, count);
    }

    /* A failed submit (device lost) ends the app, like a failed frame submit */
    int32_t Renderer::upload_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_indices)
    {
        const int32_t uploaded = terrain_.upload_dirty_chunks(vol, out_indices, max_indices, current_frame_);
        if (uploaded >= 0)
            return uploaded;
        quit_requested_ = true;
        return 0;
    }

    void Renderer::update_shadow_volume(VoxelVolume *vol)
    {
        if (!terrain_.update_shadow_volume(vol))
            quit_requested_ = true;
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
        ao_quality_ = level < 0 ? 0 : (level > 3 ? 3 : level);
    }

    void Renderer::set_denoise_quality(int level)
    {
        denoise_quality_ = level < 0 ? 0 : (level > 1 ? 1 : level);
    }

    void Renderer::set_taa_quality(int level)
    {
        taa_quality_ = level < 0 ? 0 : (level > 1 ? 1 : level);
    }

    void Renderer::set_interp_alpha(float alpha, int64_t sim_ticks)
    {
        /* How far the drawn state moved since the last frame, in steps: the particles' motion */
        const double drawn_time = static_cast<double>(sim_ticks) + static_cast<double>(alpha);
        particle_frame_ticks_ = particle_time_valid_ ? static_cast<float>(drawn_time - particle_drawn_time_) : 0.0f;
        particle_drawn_time_ = drawn_time;
        particle_time_valid_ = true;
        interp_alpha_ = alpha;
    }

    void Renderer::set_adaptive_quality(bool enabled)
    {
        bool was_enabled = adaptive_quality_;
        adaptive_quality_ = enabled;
        if (enabled && !was_enabled)
        {
            adaptive_preset_ = target_preset_;
            adaptive_cooldown_ = 0;
        }
    }

    void Renderer::set_master_preset(int preset)
    {
        if (preset < 0)
            preset = 0;
        if (preset >= QUALITY_PRESET_CUSTOM)
            return;
        if (target_preset_ != preset)
        {
            target_preset_ = preset;
            adaptive_preset_ = preset;
            adaptive_cooldown_ = 0;
        }
    }

    void Renderer::apply_preset(int preset)
    {
        if (preset < 0 || preset > QUALITY_PRESET_HIGH)
            return;
        shadow_quality_ = QUALITY_PRESETS[preset].shadow;
        shadow_contact_hardening_ = QUALITY_PRESETS[preset].shadow_contact != 0;
        ao_quality_ = QUALITY_PRESETS[preset].ao;
        denoise_quality_ = QUALITY_PRESETS[preset].denoise;
        taa_quality_ = QUALITY_PRESETS[preset].taa;
    }

    void Renderer::update_adaptive_quality()
    {
        if (!adaptive_quality_)
            return;
        if (adaptive_cooldown_ > 0)
        {
            adaptive_cooldown_--;
            return;
        }
        if (recent_frame_ms_ <= 0.0f)
            return;

        constexpr float TARGET_MS = 16.67f;         /* 60 FPS target */
        constexpr float HIGH_MS = TARGET_MS + 2.0f; /* 18.67ms - decrease quality */
        constexpr float LOW_MS = TARGET_MS - 4.0f;  /* 12.67ms - increase quality */

        const int preset = adaptive_preset_;
        if (recent_frame_ms_ > HIGH_MS && adaptive_preset_ > QUALITY_PRESET_DEFAULT)
            adaptive_preset_--;
        else if (recent_frame_ms_ < LOW_MS && adaptive_preset_ < target_preset_)
            adaptive_preset_++;
        if (adaptive_preset_ != preset)
        {
            apply_preset(adaptive_preset_);
            adaptive_cooldown_ = ADAPTIVE_COOLDOWN_FRAMES;
            /* The frames before the change say nothing about the new preset */
            recent_frame_ms_ = 0.0f;
        }
    }

    bool Renderer::begin_frame(uint32_t *image_index)
    {
        frame_started_ = false;
        swapchain_rendering_ = false;
        /* Only a frame that reaches end_frame continues the run of presents */
        present_interval_start_ = last_present_ticks_;
        last_present_ticks_ = 0;

        camera_forward_ = vec3_create(-view_matrix_.m[2], -view_matrix_.m[6], -view_matrix_.m[10]);

        if (quit_requested_ || ctx_.device() == VK_NULL_HANDLE)
            return false;

        /* Minimized (0x0 client area): there is nothing to present to */
        if (window_.width() <= 0 || window_.height() <= 0)
            return false;

        switch (swapchain_.prepare())
        {
        case Swapchain::Status::Ready:
            break;
        case Swapchain::Status::Rebuild:
            if (!rebuild_swapchain())
                return false;
            break;
        case Swapchain::Status::Skip:
            if (swapchain_.failed())
                quit_requested_ = true;
            return false;
        }

        /* Wait for this frame slot's previous submission before touching its resources */
        int64_t wait_start = platform_get_ticks();
        VkResult fence_status = vkWaitForFences(ctx_.device(), 1, &frames_.in_flight[current_frame_], VK_TRUE, UINT64_MAX);
        last_wait_fence_ms_ = ticks_to_ms(wait_start, platform_get_ticks());
        if (fence_status != VK_SUCCESS)
        {
            fprintf(stderr, "vkWaitForFences failed: %s (%d)\n", vk_result_name(fence_status), fence_status);
            quit_requested_ = true;
            return false;
        }

        /* The slot's timestamp queries are complete now */
        timer_.collect(current_frame_);

        int64_t acquire_start = platform_get_ticks();
        const bool acquired = swapchain_.acquire(frames_.image_available[current_frame_], image_index);
        last_acquire_ms_ = ticks_to_ms(acquire_start, platform_get_ticks());
        if (!acquired)
        {
            if (swapchain_.failed())
                quit_requested_ = true;
            return false;
        }

        /* Reset only now: a submit that signals this fence is guaranteed to follow */
        vkResetFences(ctx_.device(), 1, &frames_.in_flight[current_frame_]);

        image_index_ = *image_index;
        total_frame_count_++;
        terrain_draw_count_ = 0;
        ui_.begin_frame();

        /* Safe to write this slot's host-visible UBOs */
        VoxelTemporalUBO temporal{};
        temporal.prev_view_proj = mat4_multiply(prev_projection_matrix_, prev_view_matrix_);
        const Mat4 prev_camera = mat4_inverse_rigid(prev_view_matrix_);
        temporal.prev_camera_pos = {prev_camera.m[12], prev_camera.m[13], prev_camera.m[14], 1.0f};
        const VkExtent2D extent = swapchain_.extent();
        temporal.inv_render_size[0] = 1.0f / static_cast<float>(extent.width);
        temporal.inv_render_size[1] = 1.0f / static_cast<float>(extent.height);
        memcpy(frames_.temporal_ubo[current_frame_].mapped, &temporal, sizeof(VoxelTemporalUBO));
        memcpy(frames_.lighting_ubo[current_frame_].mapped, &scene_lighting_ubo_data_, sizeof(SceneLightingUBO));

        VkCommandBuffer cmd = frames_.cmd[current_frame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        timer_.reset(cmd, current_frame_);

        frame_started_ = true;
        return true;
    }

    VkClearColorValue Renderer::sky_clear_color() const
    {
        return {{scene_lighting_ubo_data_.sky_color.x, scene_lighting_ubo_data_.sky_color.y,
                 scene_lighting_ubo_data_.sky_color.z, 1.0f}};
    }

    void Renderer::begin_swapchain_rendering(uint32_t image_index, VkAttachmentLoadOp load_op)
    {
        VkCommandBuffer cmd = frames_.cmd[current_frame_];

        /* Blended UI reads the attachment; LOAD keeps the blitted lighting result */
        BarrierBatch barriers(cmd);
        if (load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
            barriers.transition(swapchain_.image(image_index), image_use::color_attachment);
        else
            barriers.discard(swapchain_.image(image_index), image_use::color_attachment);
        barriers.record();

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = swapchain_.view(image_index);
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = load_op;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = sky_clear_color();

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = swapchain_.extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;

        vkCmdBeginRendering(cmd, &rendering);
        cmd_set_viewport_scissor(cmd, swapchain_.extent());
        swapchain_rendering_ = true;
    }

    void Renderer::begin_main_pass(uint32_t image_index)
    {
        frame_context().timestamp(GpuTimestamp::RENDER_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        begin_swapchain_rendering(image_index, VK_ATTACHMENT_LOAD_OP_CLEAR);
    }

    void Renderer::end_frame(uint32_t image_index)
    {
        if (!frame_started_)
            return;
        frame_started_ = false;

        VkCommandBuffer cmd = frames_.cmd[current_frame_];

        /* The acquired image must reach PRESENT_SRC even if no pass targeted it */
        if (!swapchain_rendering_)
            begin_swapchain_rendering(image_index, VK_ATTACHMENT_LOAD_OP_CLEAR);
        vkCmdEndRendering(cmd);
        swapchain_rendering_ = false;

        BarrierBatch barriers(cmd);
        barriers.transition(swapchain_.image(image_index), image_use::present);
        barriers.record();

        frame_context().timestamp(GpuTimestamp::RENDER_END, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        VkResult result = vkEndCommandBuffer(cmd);
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "vkEndCommandBuffer failed: %s (%d)\n", vk_result_name(result), result);
            quit_requested_ = true;
            return;
        }

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore render_finished = swapchain_.render_finished(image_index);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &frames_.image_available[current_frame_];
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished;

        result = vkQueueSubmit(ctx_.graphics_queue(), 1, &submit_info, frames_.in_flight[current_frame_]);
        if (result != VK_SUCCESS)
        {
            /* The fence will never signal: stop rendering instead of hanging in the next wait */
            fprintf(stderr, "vkQueueSubmit failed: %s (%d)\n", vk_result_name(result), result);
            quit_requested_ = true;
            return;
        }
        timer_.mark_submitted(current_frame_);

        int64_t present_start = platform_get_ticks();
        swapchain_.present(image_index);
        const int64_t presented = platform_get_ticks();
        last_present_ms_ = ticks_to_ms(present_start, presented);
        if (swapchain_.failed())
            quit_requested_ = true;

        if (present_interval_start_ != 0)
        {
            const float interval_ms = ticks_to_ms(present_interval_start_, presented);
            const float weight = 1.0f - expf(-interval_ms / RECENT_FRAME_TIME_WINDOW_MS);
            recent_frame_ms_ = recent_frame_ms_ > 0.0f ? recent_frame_ms_ + (interval_ms - recent_frame_ms_) * weight : interval_ms;
        }
        last_present_ticks_ = presented;

        /* Save current matrices for next frame's temporal reprojection */
        prev_view_matrix_ = view_matrix_;
        prev_projection_matrix_ = projection_matrix_;

        current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::update_jitter()
    {
        jitter_px_[0] = 0.0f;
        jitter_px_[1] = 0.0f;
        if (taa_quality_ > 0)
        {
            const uint32_t phase = static_cast<uint32_t>(total_frame_count_ % TAA_JITTER_PHASES) + 1;
            jitter_px_[0] = halton(phase, 2) - 0.5f;
            jitter_px_[1] = halton(phase, 3) - 0.5f;
        }

        /* The UBO was written in begin_frame(), before the frame's quality settings */
        const VkExtent2D extent = swapchain_.extent();
        const float jitter_uv[2] = {jitter_px_[0] / static_cast<float>(extent.width),
                                    jitter_px_[1] / static_cast<float>(extent.height)};
        memcpy(static_cast<uint8_t *>(frames_.temporal_ubo[current_frame_].mapped) + offsetof(VoxelTemporalUBO, jitter_uv),
               jitter_uv, sizeof(jitter_uv));
    }

    Mat4 Renderer::jittered_projection() const
    {
        /* A clip-space offset of w * 2 * jitter / size moves every point by jitter pixels */
        const VkExtent2D extent = swapchain_.extent();
        const float ndc_x = 2.0f * jitter_px_[0] / static_cast<float>(extent.width);
        const float ndc_y = 2.0f * jitter_px_[1] / static_cast<float>(extent.height);
        Mat4 jittered = projection_matrix_;
        for (int column = 0; column < 4; column++)
        {
            jittered.m[column * 4 + 0] += ndc_x * jittered.m[column * 4 + 3];
            jittered.m[column * 4 + 1] += ndc_y * jittered.m[column * 4 + 3];
        }
        return jittered;
    }

    void Renderer::prepare_gbuffer_compute()
    {
        if (!terrain_.ready())
            return;
        update_jitter();

        /* Terrain into the G-buffer */
        terrain_draw_count_++;
        gbuffer_.trace_terrain(frame_context(), targets_, make_voxel_push_constants());
        terrain_traced_ = true;
    }

    void Renderer::begin_gbuffer_pass()
    {
        frame_context().timestamp(GpuTimestamp::RENDER_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }

    void Renderer::open_gbuffer_raster()
    {
        if (raster_open_)
            return;
        gbuffer_.begin_raster(frame_context(), targets_, perspective_near_, perspective_far_);
        raster_open_ = true;
    }

    /* Objects and particles are drawn over this frame's terrain trace: without one there is
     * nothing to light */
    void Renderer::render_voxel_objects_raymarched(const VoxelObjectWorld *world)
    {
        if (!objects_.ready() || !terrain_traced_)
            return;

        VoxelObjectPushConstants pc{};
        pc.view_proj = mat4_multiply(jittered_projection(), view_matrix_);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        pc.debug_mode = terrain_debug_mode_;
        pc.near_clip_radius = near_clip_radius_;

        const FrameContext frame = frame_context();
        if (!objects_.update(frame, world, interp_alpha_, pc))
        {
            quit_requested_ = true;
            return;
        }
        if (objects_.gpu_object_count() == 0)
            return;
        open_gbuffer_raster();
        objects_.draw(frame, pc);
    }

    void Renderer::render_particles_raymarched(const ParticleSystem *sys, const EnvParticleSystem *env_sys)
    {
        if (!terrain_traced_)
            return;

        const FrameContext frame = frame_context();
        if (particles_.prepare(frame, sys, env_sys, interp_alpha_, particle_frame_ticks_, camera_position_,
                               near_clip_radius_) == 0)
            return;

        ParticlePushConstants pc{};
        pc.view_proj = mat4_multiply(jittered_projection(), view_matrix_);
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.near_plane = perspective_near_;
        pc.far_plane = perspective_far_;
        open_gbuffer_raster();
        particles_.draw(frame, pc);
    }

    void Renderer::end_gbuffer_pass()
    {
        if (!terrain_traced_)
            return;

        const FrameContext frame = frame_context();
        gbuffer_.finish(frame, targets_, raster_open_);
        const VoxelPushConstants pc = make_voxel_push_constants();

        /* A pass that is off leaves the lighting a white input: lit everywhere, no occlusion */
        PROFILE_BEGIN(PROFILE_RENDER_SHADOW);
        if (shadow_quality_ >= 1 && shadow_ao_.trace_shadows(frame, targets_, pc, frames_.lighting_sets[current_frame_]))
            lighting_.set_shadow_input(current_frame_, shadow_ao_.resolve_shadows(frame, targets_, pc));
        else
            lighting_.set_shadow_input(current_frame_, lighting_.white_input());
        PROFILE_END(PROFILE_RENDER_SHADOW);

        if (ao_quality_ >= 1)
        {
            PROFILE_BEGIN(PROFILE_RENDER_AO);
            shadow_ao_.trace_ao(frame, targets_, pc);
            lighting_.set_ao_input(current_frame_, shadow_ao_.resolve_ao(frame, targets_, pc));
            PROFILE_END(PROFILE_RENDER_AO);
        }
        else
        {
            lighting_.set_ao_input(current_frame_, lighting_.white_input());
        }

        terrain_traced_ = false;
        raster_open_ = false;
    }

    void Renderer::render_deferred_lighting(uint32_t image_index)
    {
        if (!terrain_.ready())
            return;

        const FrameContext frame = frame_context();
        const VoxelPushConstants pc = make_voxel_push_constants();
        const VkDescriptorSet lighting_set = frames_.lighting_sets[current_frame_];

        if (denoise_quality_ < 1 && taa_quality_ < 1)
        {
            /* Straight into the swapchain image; the UI draws on top in the same scope */
            begin_swapchain_rendering(image_index, VK_ATTACHMENT_LOAD_OP_CLEAR);
            lighting_.draw(frame, lighting_set, pc);
            return;
        }

        lighting_.draw_to_lit_color(frame, targets_, lighting_set, pc, sky_clear_color());

        if (taa_quality_ < 1)
        {
            /* The denoise covers every pixel of the swapchain image; the UI draws on top in the same scope */
            begin_swapchain_rendering(image_index, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
            PROFILE_BEGIN(PROFILE_RENDER_DENOISE);
            post_.denoise(frame, pc);
            PROFILE_END(PROFILE_RENDER_DENOISE);
            return;
        }

        /* The denoise works on the jittered frame, whose depth and normals it compares; the TAA
         * then resolves the jitter */
        const GpuImage *taa_input = &targets_.lit_color;
        if (denoise_quality_ >= 1)
        {
            PROFILE_BEGIN(PROFILE_RENDER_DENOISE);
            post_.denoise_for_taa(frame, targets_, pc);
            PROFILE_END(PROFILE_RENDER_DENOISE);
            taa_input = &targets_.denoised;
        }
        post_.resolve_taa(frame, targets_, *taa_input, swapchain_.image(image_index), swapchain_.view(image_index), pc);

        /* The UI draws on top of the resolved frame */
        begin_swapchain_rendering(image_index, VK_ATTACHMENT_LOAD_OP_LOAD);
    }

    void Renderer::begin_ui()
    {
        if (!swapchain_rendering_)
            begin_swapchain_rendering(image_index_, VK_ATTACHMENT_LOAD_OP_CLEAR);
        ui_.begin(frames_.cmd[current_frame_]);
    }

    void Renderer::end_ui()
    {
        ui_.end(frames_.cmd[current_frame_], current_frame_);
    }

    void Renderer::draw_ui_quad_px(float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha)
    {
        ui_.quad_px(swapchain_.extent(), x_px, y_px, w_px, h_px, color, alpha);
    }

    void Renderer::draw_ui_text_px(float x_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text)
    {
        ui_.text_px(swapchain_.extent(), x_px, y_px, text_h_px, color, alpha, text);
    }

    /* The aspect of the images being rendered; a swapchain is never 0x0 */
    float Renderer::render_aspect() const
    {
        const VkExtent2D extent = swapchain_.extent();
        return static_cast<float>(extent.width) / static_cast<float>(extent.height);
    }

    void Renderer::update_perspective_projection()
    {
        const float aspect = render_aspect();
        projection_matrix_ = mat4_perspective(perspective_fov_y_degrees_ * K_DEG_TO_RAD, aspect,
                                              perspective_near_, perspective_far_);

        /* The farthest point of the near plane's visible rectangle: anything the near plane
         * clips away from the view lies closer to the camera than this */
        const float tan_half_y = tanf(perspective_fov_y_degrees_ * K_DEG_TO_RAD * 0.5f);
        near_clip_radius_ = perspective_near_ * sqrtf(1.0f + tan_half_y * tan_half_y * (1.0f + aspect * aspect));
    }

    void Renderer::set_perspective(float fov_y_degrees, float near_val, float far_val)
    {
        perspective_fov_y_degrees_ = fov_y_degrees;
        perspective_near_ = near_val;
        perspective_far_ = far_val;
        update_perspective_projection();
    }

    void Renderer::on_resize()
    {
        /* Settled in begin_frame(): a restore or the initial ShowWindow reports the same size
         * and rebuilds nothing; a new size rebuilds the swapchain and every target, and only
         * then changes the projection, so the frame on screen and its camera always agree */
        swapchain_.request_resize_check();
    }

    void Renderer::set_look_at(Vec3 eye, Vec3 target)
    {
        camera_position_ = eye;
        view_matrix_ = mat4_look_at(eye, target, vec3_create(0.0f, 1.0f, 0.0f));
    }

    /* Window pixels map onto the rendered image, whose size and projection follow the window */
    void Renderer::screen_to_ray(float screen_x, float screen_y, Vec3 *out_origin, Vec3 *out_dir) const
    {
        const VkExtent2D extent = swapchain_.extent();
        float nx = (2.0f * screen_x / static_cast<float>(extent.width)) - 1.0f;
        float ny = 1.0f - (2.0f * screen_y / static_cast<float>(extent.height));

        Mat4 inv_view = mat4_inverse_rigid(view_matrix_);

        float tan_half = tanf((perspective_fov_y_degrees_ * K_DEG_TO_RAD) * 0.5f);

        Vec3 dir_view = vec3_create(nx * render_aspect() * tan_half, ny * tan_half, -1.0f);

        if (out_origin)
            *out_origin = camera_position_;
        if (out_dir)
            *out_dir = vec3_normalize(mat4_transform_direction(inv_view, dir_view));
    }

    void Renderer::set_scene_lighting(const SceneLighting *lighting)
    {
        scene_lighting_ubo_data_.sun_dir = {lighting->sun_direction.x, lighting->sun_direction.y,
                                            lighting->sun_direction.z, lighting->wrap_bias};
        scene_lighting_ubo_data_.sun_color = {lighting->sun_color.x, lighting->sun_color.y,
                                              lighting->sun_color.z, lighting->sun_strength};
        scene_lighting_ubo_data_.fill_dir = {lighting->fill_direction.x, lighting->fill_direction.y,
                                             lighting->fill_direction.z, lighting->fill_strength};
        scene_lighting_ubo_data_.fill_color = {lighting->fill_color.x, lighting->fill_color.y,
                                               lighting->fill_color.z, lighting->back_strength};
        scene_lighting_ubo_data_.back_dir = {lighting->back_direction.x, lighting->back_direction.y,
                                             lighting->back_direction.z, lighting->rim_strength};
        scene_lighting_ubo_data_.back_color = {lighting->back_color.x, lighting->back_color.y,
                                               lighting->back_color.z, lighting->ambient_strength};
        scene_lighting_ubo_data_.sky_ambient = {lighting->sky_ambient_color.x, lighting->sky_ambient_color.y,
                                                lighting->sky_ambient_color.z, lighting->exposure};
        scene_lighting_ubo_data_.ground_ambient = {lighting->ground_ambient_color.x, lighting->ground_ambient_color.y,
                                                   lighting->ground_ambient_color.z, lighting->emissive_multiplier};
        scene_lighting_ubo_data_.sky_color = {lighting->sky_color.x, lighting->sky_color.y,
                                              lighting->sky_color.z, lighting->shadow_max_dist};
    }

    /* Push constants shared by the G-buffer, shadow, AO, temporal, TAA, denoise and
     * lighting passes (each shader reads the subset it needs). */
    VoxelPushConstants Renderer::make_voxel_push_constants() const
    {
        const VolumeParams &volume = terrain_.params();
        VoxelPushConstants pc{};
        pc.inv_view = mat4_inverse_rigid(view_matrix_);
        pc.inv_projection = mat4_inverse(jittered_projection());
        for (int i = 0; i < 3; i++)
        {
            pc.bounds_min[i] = volume.bounds_min[i];
            pc.bounds_max[i] = volume.bounds_max[i];
            pc.grid_size[i] = volume.grid_size[i];
            pc.chunks_dim[i] = volume.chunks_dim[i];
        }
        pc.voxel_size = volume.voxel_size;
        pc.max_steps = terrain_.raymarch_max_steps();
        pc.camera_pos[0] = camera_position_.x;
        pc.camera_pos[1] = camera_position_.y;
        pc.camera_pos[2] = camera_position_.z;
        pc.history_valid = 0;
        pc.total_chunks = volume.total_chunks;
        /* Wrapped so that float(frame_count) * k keeps its fraction in the shaders */
        pc.frame_count = static_cast<int32_t>(total_frame_count_ & 0xFFFFu);
        pc.debug_mode = terrain_debug_mode_;
        pc.far_plane = perspective_far_;
        pc.object_count = objects_.gpu_object_count();
        pc.shadow_quality = shadow_quality_;
        pc.shadow_contact = shadow_contact_hardening_ ? 1 : 0;
        pc.ao_quality = ao_quality_;
        return pc;
    }

}
