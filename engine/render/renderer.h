#ifndef PATCH_ENGINE_RENDERER_H
#define PATCH_ENGINE_RENDERER_H

#include <vulkan/vulkan.h>
#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/physics/particles.h"
#include "engine/sim/env_particles.h"
#include "engine/sim/loading.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/render/voxel_push_constants.h"
#include "engine/render/vk_context.h"
#include "engine/render/vk_swapchain.h"
#include "engine/render/frame_resources.h"
#include "engine/render/gpu_timer.h"
#include "engine/render/render_targets.h"
#include "engine/render/terrain_volume.h"
#include "engine/render/gbuffer_pass.h"
#include "engine/render/shadow_ao_pass.h"
#include "engine/render/lighting_pass.h"
#include "engine/render/post_pass.h"
#include "engine/render/voxel_objects.h"
#include "engine/render/particle_pass.h"
#include "engine/render/ui_pass.h"
#include "engine/platform/window.h"
#include "content/scenes.h"
#include <cstdint>

namespace patch
{

    /*
     * Facade over the Vulkan subsystems: it owns them, sequences their work within a frame and
     * keeps the per-frame state they share (camera, quality settings, frame slot). Resources
     * live in three groups with their own lifetimes: device lifetime (pipelines, layouts,
     * descriptor sets, per-frame buffers), size dependent (RenderTargets and the descriptor
     * writes that reference them, see bind_targets()) and per scene (terrain, voxel objects).
     */
    class Renderer
    {
    public:
        using GPUTimings = GpuTimings;

        Renderer(Window &window);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;
        Renderer(Renderer &&) = delete;
        Renderer &operator=(Renderer &&) = delete;

        bool init();
        const char *get_init_error() const { return init_error_; }

        /* DEBUG: Accessors for debug overlay */
        void DEBUG_set_terrain_debug_mode(int mode) { terrain_debug_mode_ = mode; }
        int DEBUG_get_terrain_debug_mode() const { return terrain_debug_mode_; }
        int DEBUG_get_terrain_draw_count() const { return terrain_draw_count_; }

        void set_material_palette(const MaterialEntry *materials, int32_t count);
        void set_scene_lighting(const SceneLighting *lighting);

        /*
         * Frame protocol: begin_frame() returns false when no frame can be rendered
         * (window minimized, swapchain out of date and being recreated, device lost).
         * In that case the caller must skip every recording call for this iteration,
         * including end_frame(). When it returns true, end_frame() must be called.
         * A frame after a resize is rendered at the new size: begin_frame() rebuilds the
         * swapchain and every size-dependent resource first.
         */
        bool begin_frame(uint32_t *image_index);
        void begin_main_pass(uint32_t image_index);
        void end_frame(uint32_t image_index);

        /* Set when the renderer cannot continue (device lost, failed swapchain or render target
         * rebuild, failed scene resource creation). The app should close its window and shut
         * down normally. */
        bool quit_requested() const { return quit_requested_; }

        /* Deferred rendering, in call order within a frame. The objects and particles draw on
         * top of the terrain trace; the raster pass for them (depth prime and the G-buffer
         * attachments) runs only when one of them has something to draw. */
        void prepare_gbuffer_compute();
        void begin_gbuffer_pass();
        void render_voxel_objects_raymarched(const VoxelObjectWorld *world);
        void render_particles_raymarched(const ParticleSystem *sys, const EnvParticleSystem *env_sys);
        void end_gbuffer_pass();
        void render_deferred_lighting(uint32_t image_index);

        /*
         * Scene resources, loaded behind the loading screen: begin_scene_load() (the terrain and
         * room for max_objects voxel objects), then poll_scene_load() once per loading screen
         * frame until it returns true. The terrain's CPU work (shadow volume packing, chunk
         * staging) runs on worker threads meanwhile, as stages of state, so the first frame of
         * the scene follows the loading screen without a wait. Both set quit_requested() on
         * failure (and poll_scene_load() then returns true).
         */
        static constexpr int32_t SCENE_LOAD_STAGES = 2;
        void prepare_for_scene_switch();
        void begin_scene_load(VoxelVolume *vol, uint32_t max_objects, LoadingState *state);
        bool poll_scene_load(LoadingState *state);

        /*
         * Upload dirty chunks to GPU. Returns the number of indices written to out_indices.
         * Caller must call volume_mark_chunks_uploaded() with them after this returns
         * to avoid re-uploading the same chunks next frame.
         */
        int32_t upload_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_indices);

        /* Repack and upload the dirty parts of the terrain shadow volume */
        void update_shadow_volume(VoxelVolume *vol);

        /* Into the swapchain image's rendering scope; opens one (cleared) when no pass did */
        void begin_ui();
        void end_ui();

        /* Pixel-space UI helpers (top-left origin, sizes in pixels). */
        void draw_ui_quad_px(float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha);
        void draw_ui_text_px(float x_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text);

        static float ui_text_width_px(const char *text, float text_h_px)
        {
            if (!text || text_h_px <= 0.0f)
                return 0.0f;
            int32_t len = 0;
            for (const char *p = text; *p; ++p)
                ++len;
            const float unit_px = ui_text_unit_px(text_h_px);
            return (float)len * unit_px * 6.0f - unit_px;
        }

        void set_perspective(float fov_y_degrees, float near_val, float far_val);
        void set_look_at(Vec3 eye, Vec3 target);
        /* Every WM_SIZE, including minimize and restore */
        void on_resize();
        void screen_to_ray(float screen_x, float screen_y, Vec3 *out_origin, Vec3 *out_dir) const;

        /* Size of the images being rendered (the swapchain's) */
        VkExtent2D get_render_extent() const { return swapchain_.extent(); }

        Vec3 get_camera_position() const { return camera_position_; }
        Vec3 get_camera_forward() const { return camera_forward_; }

        void set_shadow_quality(int level);
        void set_shadow_contact_hardening(bool enabled);
        void set_ao_quality(int level);
        void set_denoise_quality(int level);
        void set_taa_quality(int level);

        /* Interpolation of objects and particles: alpha blends the last two of the sim_ticks
         * simulation steps taken so far (the scene's count, so the particles' motion vectors
         * follow it) */
        void set_interp_alpha(float alpha, int64_t sim_ticks);

        /* Adaptive quality control: steps the preset by the recent frame time (see
         * recent_frame_ms_), once per app loop iteration */
        void set_adaptive_quality(bool enabled);
        bool get_adaptive_quality() const { return adaptive_quality_; }
        void update_adaptive_quality();

        /* Master quality preset control */
        void set_master_preset(int preset);
        void apply_preset(int preset);

        bool get_last_gpu_timings(GPUTimings *out_timings) const { return timer_.last(out_timings); }
        const char *get_gpu_name() const { return ctx_.gpu_name(); }
        float get_last_wait_fence_ms() const { return last_wait_fence_ms_; }
        float get_last_acquire_ms() const { return last_acquire_ms_; }
        float get_last_present_ms() const { return last_present_ms_; }

    private:
        void cleanup();
        /* Every descriptor write that references a render target */
        void bind_targets();
        bool rebuild_swapchain();
        void reset_scene_state();
        void reset_temporal_history();
        /* Stops a scene load still running (its stage finishes first) */
        void abandon_scene_load();
        static void run_scene_load_stage(void *renderer, int32_t stage);

        FrameContext frame_context() const
        {
            return {frames_.cmd[current_frame_], current_frame_, total_frame_count_, &timer_};
        }
        void begin_swapchain_rendering(uint32_t image_index, VkAttachmentLoadOp load_op);
        /* Depth prime and the raster scope, at the first object or particle draw of a frame */
        void open_gbuffer_raster();
        VkClearColorValue sky_clear_color() const;

        float render_aspect() const;
        void update_perspective_projection();
        /* This frame's TAA jitter (none with TAA off), also into the temporal UBO */
        void update_jitter();
        /* projection_matrix_ offset by the jitter: what every pass renders this frame with */
        Mat4 jittered_projection() const;
        VoxelPushConstants make_voxel_push_constants() const;

        Window &window_;
        const char *init_error_ = nullptr;

        VulkanContext ctx_;
        Swapchain swapchain_;
        FrameResources frames_;
        GpuTimer timer_;
        RenderTargets targets_;
        TerrainVolume terrain_;
        /* The terrain's CPU work while it loads (begin_scene_load() to poll_scene_load()) */
        JobSystem *scene_load_jobs_ = nullptr;
        BackgroundLoad *scene_load_ = nullptr;
        bool scene_uploading_ = false; /* its CPU work is done, the GPU upload not yet */
        GBufferPass gbuffer_;
        ShadowAoPass shadow_ao_;
        LightingPass lighting_;
        PostPass post_;
        VoxelObjects objects_;
        ParticlePass particles_;
        UiPass ui_;

        uint32_t current_frame_ = 0;
        uint32_t image_index_ = 0;         /* swapchain image this frame renders to */
        bool quit_requested_ = false;
        bool frame_started_ = false;       /* begin_frame succeeded, end_frame not yet called */
        bool swapchain_rendering_ = false; /* a rendering scope targeting the swapchain image is open */
        bool terrain_traced_ = false;      /* the compute raymarch filled this frame's G-buffer */
        bool raster_open_ = false;         /* ... and the raster scope for objects / particles is open */

        SceneLightingUBO scene_lighting_ubo_data_{};

        Mat4 view_matrix_ = mat4_identity();
        Mat4 projection_matrix_ = mat4_identity(); /* without the TAA jitter */
        float jitter_px_[2] = {0.0f, 0.0f};        /* this frame's projection offset, in pixels */
        Mat4 prev_view_matrix_ = mat4_identity();       /* Previous frame for temporal reprojection */
        Mat4 prev_projection_matrix_ = mat4_identity(); /* Previous frame for temporal reprojection */
        uint64_t total_frame_count_ = 0;                /* Frames begun (FrameContext::number) */
        float perspective_fov_y_degrees_ = 60.0f;
        float perspective_near_ = 0.1f;
        float perspective_far_ = 200.0f;
        float near_clip_radius_ = 0.0f; /* the near plane cuts the view this close to the camera */
        Vec3 camera_position_ = vec3_zero();
        Vec3 camera_forward_ = vec3_zero();

        bool adaptive_quality_ = false; /* Dynamic quality adjustment (default off) */
        int adaptive_cooldown_ = 0;     /* Frames until next quality change allowed */
        static constexpr int ADAPTIVE_COOLDOWN_FRAMES = 30;
        /* Interval between consecutive presents, averaged exponentially over about this long
         * whatever the frame rate; 0 until two frames in a row were presented. A frame not
         * rendered (minimized, swapchain rebuild) breaks the run, and a new scene starts over:
         * its loading frames say nothing about it. */
        static constexpr float RECENT_FRAME_TIME_WINDOW_MS = 250.0f;
        float recent_frame_ms_ = 0.0f;
        int64_t last_present_ticks_ = 0;     /* 0: the last frame was not presented */
        int64_t present_interval_start_ = 0; /* last_present_ticks_ when this frame began */
        int target_preset_ = QUALITY_PRESET_FAIR;   /* User-selected max preset */
        int adaptive_preset_ = QUALITY_PRESET_FAIR; /* Current preset when adaptive on */
        int shadow_quality_ = QUALITY_DEFAULT_SHADOW;
        bool shadow_contact_hardening_ = QUALITY_DEFAULT_SHADOW_CONTACT;
        int ao_quality_ = QUALITY_DEFAULT_AO;
        int denoise_quality_ = QUALITY_DEFAULT_DENOISE;
        int taa_quality_ = QUALITY_DEFAULT_TAA;
        float interp_alpha_ = 0.0f;  /* Interpolation factor between the last two sim steps */
        float particle_frame_ticks_ = 0.0f; /* sim steps the drawn particles advanced since the last frame */
        double particle_drawn_time_ = 0.0;  /* sim steps (with alpha) drawn last frame */
        bool particle_time_valid_ = false;  /* ... known (a frame of this scene was drawn) */
        int terrain_debug_mode_ = 0; /* DEBUG: shader debug view (see TERRAIN_DEBUG_MODE_NAMES) */
        int terrain_draw_count_ = 0; /* DEBUG: G-buffer dispatches this frame */

        float last_wait_fence_ms_ = 0.0f;
        float last_acquire_ms_ = 0.0f;
        float last_present_ms_ = 0.0f;
    };

}

#endif
