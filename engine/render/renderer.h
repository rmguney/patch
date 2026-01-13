#ifndef PATCH_ENGINE_RENDERER_H
#define PATCH_ENGINE_RENDERER_H

#include <vulkan/vulkan.h>
#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/physics/particles.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/render/draw_list.h"
#include "engine/platform/window.h"
#include <cstdint>
#include <vector>

namespace patch
{

    /* Material data for GPU upload (matches GPUMaterialColor layout) */
    struct MaterialEntry
    {
        float r, g, b, emissive;
        float roughness, metallic, flags, pad;
    };

    struct VulkanBuffer
    {
        VkBuffer buffer;
        VkDeviceMemory memory;
    };

    struct MeshBuffers
    {
        VulkanBuffer vertex;
        VulkanBuffer index;
        uint32_t index_count;
    };

    /* GPU instance data for batched box rendering */
    struct alignas(16) BoxInstanceGPU
    {
        float model_col0[4]; /* mat4 column 0 */
        float model_col1[4]; /* mat4 column 1 */
        float model_col2[4]; /* mat4 column 2 */
        float model_col3[4]; /* mat4 column 3 */
        float color_alpha[4];
        float params[4];
    };
    static_assert(sizeof(BoxInstanceGPU) == 96, "BoxInstanceGPU must be 96 bytes");

    /* GPU metadata for raymarched voxel objects */
    struct alignas(16) VoxelObjectGPU
    {
        float world_to_local[16]; /* mat4: transform ray to object space */
        float local_to_world[16]; /* mat4: transform hit back to world */
        float bounds_min[4];      /* Object AABB min (xyz), voxel_size (w) */
        float bounds_max[4];      /* Object AABB max (xyz), grid_size (w) */
        float position[4];        /* World position (xyz), active flag (w) */
        uint32_t atlas_slice;     /* Z-slice in 3D atlas */
        uint32_t material_base;   /* Base material offset (for future palette per-object) */
        uint32_t flags;           /* Bitflags: sleeping, dirty, etc. */
        uint32_t occupancy_mask;  /* 8-bit region occupancy (2×2×2 regions of 8³) */
    };
    static_assert(sizeof(VoxelObjectGPU) == 192, "VoxelObjectGPU must be 192 bytes");

    class Renderer
    {
    public:
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

        static constexpr uint32_t GBUFFER_ALBEDO = 0;
        static constexpr uint32_t GBUFFER_NORMAL = 1;
        static constexpr uint32_t GBUFFER_MATERIAL = 2;
        static constexpr uint32_t GBUFFER_LINEAR_DEPTH = 3;
        static constexpr uint32_t GBUFFER_COUNT = 4;

        Renderer(Window &window);
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;
        Renderer(Renderer &&) = delete;
        Renderer &operator=(Renderer &&) = delete;

        bool init();
        const char *get_init_error() const { return init_error_; }

        /* DEBUG: Accessors for debug overlay */
        bool DEBUG_is_gbuffer_initialized() const { return gbuffer_initialized_; }
        bool DEBUG_is_gbuffer_pipeline_valid() const { return gbuffer_pipeline_ != VK_NULL_HANDLE; }
        bool DEBUG_is_gbuffer_descriptors_valid() const { return gbuffer_descriptor_sets_[0] != VK_NULL_HANDLE; }
        bool DEBUG_is_voxel_resources_initialized() const { return voxel_resources_initialized_; }
        bool DEBUG_is_vobj_resources_initialized() const { return vobj_resources_initialized_; }
        void DEBUG_set_terrain_debug_mode(int mode) { terrain_debug_mode_ = mode; }
        int DEBUG_get_terrain_debug_mode() const { return terrain_debug_mode_; }
        int DEBUG_get_terrain_draw_count() const { return terrain_draw_count_; }

        void set_material_palette(const Vec3 *colors, int32_t count);
        void set_material_palette_full(const MaterialEntry *materials, int32_t count);

        void begin_frame(uint32_t *image_index);
        void begin_main_pass(uint32_t image_index);
        void end_frame(uint32_t image_index);

        /* Deferred rendering pass methods */
        void prepare_gbuffer_compute(const VoxelVolume *vol, bool has_objects_or_particles); /* Call before begin_gbuffer_pass */
        void begin_gbuffer_pass();
        void end_gbuffer_pass();
        void render_gbuffer_terrain(const VoxelVolume *vol);
        void render_deferred_lighting(uint32_t image_index);

        void init_volume_for_raymarching(const VoxelVolume *vol);
        /*
         * Upload dirty chunks to GPU. Returns count of chunks uploaded.
         * Caller must call volume_mark_chunks_uploaded() after this returns
         * to avoid re-uploading the same chunks next frame.
         */
        int32_t upload_dirty_chunks(const VoxelVolume *vol, int32_t *out_indices, int32_t max_indices);

        /* Update shadow volume from current terrain state (call after terrain changes) */
        void update_shadow_volume(VoxelVolume *vol,
                                  const VoxelObjectWorld *objects = nullptr,
                                  const ParticleSystem *particles = nullptr);

        /* GPU raymarched particle rendering */
        void render_particles_raymarched(const ParticleSystem *sys);

        /* GPU raymarched voxel object rendering (Phase 1.5) */
        void render_voxel_objects_raymarched(const VoxelObjectWorld *world);
        bool init_voxel_object_resources(uint32_t max_objects);
        void destroy_voxel_object_resources();
        void mark_vobj_dirty(uint32_t index);
        bool is_vobj_dirty(uint32_t index) const;
        void clear_vobj_dirty(uint32_t index);

        void begin_ui();
        void end_ui();
        void draw_ui_quad(float cx, float cy, float w, float h, Vec3 color, float alpha);
        void draw_ui_text(float x, float y, float pixel, Vec3 color, float alpha, const char *text);

        /* Pixel-space UI helpers (top-left origin, sizes in pixels). */
        void draw_ui_quad_px(float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha);
        void draw_ui_text_px(float x_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text);

        static inline float ui_text_width_px(const char *text, float text_h_px)
        {
            if (!text || text_h_px <= 0.0f)
                return 0.0f;
            int32_t len = 0;
            for (const char *p = text; *p; ++p)
                ++len;
            const float unit_px = text_h_px / 7.0f;
            return (float)len * unit_px * 6.0f - unit_px;
        }

        void set_orthographic(float width, float height, float depth);
        void set_perspective(float fov_y_degrees, float near_val, float far_val);
        void set_view_angle(float yaw_degrees, float distance);
        void set_view_angle_at(float yaw_degrees, float distance, Vec3 target);
        void set_view_angle_at(float yaw_degrees, float distance, Vec3 target, float dt);
        void set_look_at(Vec3 eye, Vec3 target);
        void set_look_at(Vec3 eye, Vec3 target, float dt);
        void on_resize();
        bool screen_to_world_floor(float screen_x, float screen_y, float floor_y, Vec3 *out_world) const;
        void screen_to_ray(float screen_x, float screen_y, Vec3 *out_origin, Vec3 *out_dir) const;

        Vec3 get_camera_position() const { return camera_position_; }
        Vec3 get_camera_forward() const { return camera_forward_; }
        const Frustum &get_frustum() const { return frustum_; }
        bool is_rt_supported() const { return rt_supported_; }

        /* Frustum culling utilities */
        bool is_chunk_visible(int32_t cx, int32_t cy, int32_t cz,
                              const VoxelVolume *vol) const;
        int get_rt_quality() const { return rt_quality_; }
        void set_rt_quality(int level);

    public:
        enum class PresentMode
        {
            VSync,
            Mailbox,
            Immediate
        };
        PresentMode get_present_mode() const { return present_mode_; }

    private:
        enum class ProjectionMode
        {
            Orthographic,
            Perspective
        };

        void update_perspective_projection();
        bool create_instance();
        bool select_physical_device();
        bool find_queue_families();
        bool create_logical_device();
        bool create_swapchain();
        bool create_render_pass();
        bool create_depth_resources();
        bool create_framebuffers();
        bool create_command_pool();
        bool create_sync_objects();
        bool recreate_swapchain();
        void destroy_swapchain_objects();
        bool create_pipeline(const uint32_t *vert_code, size_t vert_size,
                             const uint32_t *frag_code, size_t frag_size,
                             bool enable_blend, bool depth_write,
                             VkCullModeFlags cull_mode, VkPipeline *out_pipeline);
        bool create_pipelines();
        bool create_voxel_descriptor_layout();
        bool create_voxel_descriptors(int32_t total_chunks);
        void update_voxel_depth_descriptor();
        bool create_compute_pipeline(const uint32_t *code, size_t code_size,
                                     VkPipelineLayout layout, VkPipeline *out_pipeline);

        void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties, VulkanBuffer *buffer);
        void destroy_buffer(VulkanBuffer *buffer);
        uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

        void create_quad_mesh();

        void cleanup();

        Window &window_;

        VkInstance instance_;
        VkPhysicalDevice physical_device_;
        VkDevice device_;
        VkQueue graphics_queue_;
        VkQueue present_queue_;
        uint32_t graphics_family_;
        uint32_t present_family_;

        VkSurfaceKHR surface_;
        VkSwapchainKHR swapchain_;
        VkFormat swapchain_format_;
        VkExtent2D swapchain_extent_;

        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_image_views_;
        std::vector<VkFramebuffer> framebuffers_;

        VkRenderPass render_pass_;
        VkPipelineLayout pipeline_layout_;
        VkPipeline ui_pipeline_;

        /* Lighting uniforms UBO (used by deferred lighting shader) */
        VulkanBuffer lighting_ubo_[MAX_FRAMES_IN_FLIGHT];

        Vec3 camera_target_;
        Vec3 prev_camera_target_;
        bool camera_initialized_;

        VkCommandPool command_pool_;
        VkCommandBuffer command_buffers_[MAX_FRAMES_IN_FLIGHT];

        VkSemaphore image_available_semaphores_[MAX_FRAMES_IN_FLIGHT];
        VkSemaphore render_finished_semaphores_[MAX_FRAMES_IN_FLIGHT];
        VkFence in_flight_fences_[MAX_FRAMES_IN_FLIGHT];

        /* Timeline semaphore for async uploads */
        VkSemaphore upload_timeline_semaphore_;
        uint64_t upload_timeline_value_;

        /* Deferred destruction for staging buffers */
        static constexpr uint32_t MAX_PENDING_DESTROYS = 8;
        struct PendingDestroy
        {
            VulkanBuffer buffer;
            uint64_t timeline_value;
        };
        PendingDestroy pending_destroys_[MAX_PENDING_DESTROYS];
        uint32_t pending_destroy_count_;

        /* Reusable upload command buffer */
        VkCommandBuffer upload_cmd_;

        uint32_t current_frame_;

        VkImage depth_image_;
        VkDeviceMemory depth_image_memory_;
        VkImageView depth_image_view_;
        VkSampler depth_sampler_;

        MeshBuffers quad_mesh_;

        Mat4 view_matrix_;
        Mat4 projection_matrix_;
        Mat4 prev_view_matrix_;       /* Previous frame for temporal reprojection */
        Mat4 prev_projection_matrix_; /* Previous frame for temporal reprojection */
        uint32_t total_frame_count_;  /* Total frames rendered (for temporal effects) */
        float ortho_base_width_;
        float ortho_base_height_;
        float ortho_base_depth_;
        float ortho_half_width_;
        float ortho_half_height_;
        ProjectionMode projection_mode_;
        PresentMode present_mode_ = PresentMode::Mailbox; /* Default: uncapped FPS */
        float perspective_fov_y_degrees_;
        float perspective_near_;
        float perspective_far_;
        Vec3 camera_position_;
        Vec3 camera_forward_;
        Frustum frustum_;
        const char *init_error_;

        Vec3 material_palette_[VOXEL_MATERIAL_MAX];
        MaterialEntry material_entries_[VOXEL_MATERIAL_MAX];
        int32_t material_count_;
        bool use_full_materials_;

        /* Voxel ray rendering resources */
        VkDescriptorSetLayout voxel_descriptor_layout_;
        VkDescriptorPool voxel_descriptor_pool_;
        VkDescriptorSet voxel_descriptor_sets_[MAX_FRAMES_IN_FLIGHT];

        VulkanBuffer voxel_data_buffer_;                        /* SSBO: chunk voxel material IDs */
        VulkanBuffer voxel_headers_buffer_;                     /* SSBO: chunk occupancy headers */
        VulkanBuffer voxel_material_buffer_;                    /* UBO: material palette */
        VulkanBuffer voxel_temporal_ubo_[MAX_FRAMES_IN_FLIGHT]; /* UBO: prev_view_proj */

        /* Persistent staging buffers for chunk uploads (avoids per-frame allocation) */
        VulkanBuffer staging_voxels_buffer_;
        VulkanBuffer staging_headers_buffer_;
        void *staging_voxels_mapped_ = nullptr;
        void *staging_headers_mapped_ = nullptr;

        int32_t voxel_total_chunks_ = 0;
        bool voxel_resources_initialized_ = false;

        bool rt_supported_ = false;
        int rt_quality_ = 1;                 /* 0=Off, 1=Fair, 2=Good, 3=High */
        int terrain_debug_mode_ = 0;         /* DEBUG: 0=normal, 1=AABB visualization */
        mutable int terrain_draw_count_ = 0; /* DEBUG: Count of terrain draw calls */

        /* Compute shader infrastructure for temporal shadow resolve */
        VkPipeline temporal_compute_pipeline_;
        VkPipelineLayout temporal_compute_layout_;

        VkDescriptorSetLayout temporal_shadow_input_layout_;  /* Set 0: depth/normal/motion/current/history */
        VkDescriptorSetLayout temporal_shadow_output_layout_; /* Set 1: resolved shadow output */
        VkDescriptorPool temporal_shadow_descriptor_pool_;
        VkDescriptorSet temporal_shadow_input_sets_[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet temporal_shadow_output_sets_[MAX_FRAMES_IN_FLIGHT];
        bool temporal_shadow_history_valid_ = false;

        /* Unified raymarching compute pipelines */
        VkPipeline gbuffer_compute_pipeline_;
        VkPipelineLayout gbuffer_compute_layout_;
        VkDescriptorSetLayout gbuffer_compute_terrain_layout_; /* Set 0: terrain data */
        VkDescriptorSetLayout gbuffer_compute_vobj_layout_;    /* Set 1: voxel objects */
        VkDescriptorSetLayout gbuffer_compute_output_layout_;  /* Set 2: G-buffer outputs */
        VkDescriptorPool gbuffer_compute_descriptor_pool_;
        VkDescriptorSet gbuffer_compute_terrain_sets_[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet gbuffer_compute_vobj_sets_[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet gbuffer_compute_output_sets_[MAX_FRAMES_IN_FLIGHT];

        VkPipeline shadow_compute_pipeline_;
        VkPipelineLayout shadow_compute_layout_;
        VkDescriptorSetLayout shadow_compute_input_layout_;   /* Set 0: chunk headers + shadow vol */
        VkDescriptorSetLayout shadow_compute_gbuffer_layout_; /* Set 1: G-buffer depth/normal */
        VkDescriptorSetLayout shadow_compute_output_layout_;  /* Set 2: shadow output */
        VkDescriptorPool shadow_compute_descriptor_pool_;
        VkDescriptorSet shadow_compute_input_sets_[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet shadow_compute_gbuffer_sets_[MAX_FRAMES_IN_FLIGHT];
        VkDescriptorSet shadow_compute_output_sets_[MAX_FRAMES_IN_FLIGHT];

        /* Shadow output buffer for compute pass */
        VkImage shadow_output_image_;
        VkDeviceMemory shadow_output_memory_;
        VkImageView shadow_output_view_;

        bool compute_raymarching_enabled_ = true; /* Use compute path when available */
        bool compute_resources_initialized_ = false;
        bool gbuffer_compute_dispatched_ = false; /* Set when compute fills gbuffer this frame */

    public:
        void set_compute_raymarching_enabled(bool enabled) { compute_raymarching_enabled_ = enabled; }
        bool is_compute_raymarching_enabled() const { return compute_raymarching_enabled_ && compute_resources_initialized_; }

    private:
        /* History buffers for temporal accumulation (ping-pong) */
        VkImage history_images_[2];
        VkDeviceMemory history_image_memory_[2];
        VkImageView history_image_views_[2];
        int history_write_index_;

        /* State tracking to skip redundant binds */
        VkPipeline last_bound_pipeline_;
        VkDescriptorSet last_bound_descriptor_set_;

        /* Helpers for state-tracked binding */
        void bind_pipeline(VkPipeline pipeline);
        void bind_descriptor_set(VkDescriptorSet set);
        void reset_bind_state();

        /* G-buffer resources for deferred rendering */
        VkImage gbuffer_images_[GBUFFER_COUNT];
        VkDeviceMemory gbuffer_memory_[GBUFFER_COUNT];
        VkImageView gbuffer_views_[GBUFFER_COUNT];
        VkSampler gbuffer_sampler_;
        VkRenderPass gbuffer_render_pass_;
        VkRenderPass gbuffer_render_pass_load_; /* Uses LOAD_OP_LOAD for post-compute */
        VkFramebuffer gbuffer_framebuffer_;
        VkDescriptorSetLayout gbuffer_descriptor_layout_;
        VkDescriptorPool gbuffer_descriptor_pool_;
        VkDescriptorSet gbuffer_descriptor_sets_[MAX_FRAMES_IN_FLIGHT];
        VkPipeline gbuffer_pipeline_;
        VkPipelineLayout gbuffer_pipeline_layout_;

        /* Deferred lighting pass */
        VkPipeline deferred_lighting_pipeline_;
        VkPipelineLayout deferred_lighting_layout_;
        VkDescriptorSetLayout deferred_lighting_descriptor_layout_;
        VkDescriptorPool deferred_lighting_descriptor_pool_;
        VkDescriptorSet deferred_lighting_descriptor_sets_[MAX_FRAMES_IN_FLIGHT];

        /* Shadow volume for sparse RT tracing */
        VkImage shadow_volume_image_;
        VkDeviceMemory shadow_volume_memory_;
        VkImageView shadow_volume_view_;
        VkSampler shadow_volume_sampler_;
        uint32_t shadow_volume_dims_[3];
        uint32_t shadow_volume_last_frame_;

        std::vector<uint8_t> shadow_mip0_;
        std::vector<uint8_t> shadow_mip1_;
        std::vector<uint8_t> shadow_mip2_;
        uint32_t shadow_mip_dims_[3][3];
        bool shadow_volume_initialized_;

        /* Blue noise texture for temporal sampling */
        VkImage blue_noise_image_;
        VkDeviceMemory blue_noise_memory_;
        VkImageView blue_noise_view_;
        VkSampler blue_noise_sampler_;

        /* Motion vectors for temporal reprojection */
        VkImage motion_vector_image_;
        VkDeviceMemory motion_vector_memory_;
        VkImageView motion_vector_view_;

        /* Voxel object GPU raymarching (Phase 1.5) */
        static constexpr uint32_t VOBJ_ATLAS_MAX_OBJECTS = 512;
        static constexpr uint32_t VOBJ_GRID_DIM = 16;
        VkImage vobj_atlas_image_;
        VkDeviceMemory vobj_atlas_memory_;
        VkImageView vobj_atlas_view_;
        VkSampler vobj_atlas_sampler_;
        VulkanBuffer vobj_metadata_buffer_;
        void *vobj_metadata_mapped_;
        VulkanBuffer vobj_staging_buffer_;
        void *vobj_staging_mapped_;

        VkPipeline vobj_pipeline_;
        VkPipelineLayout vobj_pipeline_layout_;
        VkDescriptorSetLayout vobj_descriptor_layout_;
        VkDescriptorPool vobj_descriptor_pool_;
        VkDescriptorSet vobj_descriptor_sets_[MAX_FRAMES_IN_FLIGHT];

        uint32_t vobj_max_objects_ = 0;
        uint32_t vobj_dirty_mask_[(VOBJ_ATLAS_MAX_OBJECTS + 31) / 32] = {};
        int32_t vobj_voxel_count_cache_[VOBJ_ATLAS_MAX_OBJECTS] = {};
        bool vobj_resources_initialized_ = false;

        /* Raymarched particle resources */
        struct SizedBuffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
        };
        SizedBuffer particle_ssbo_;
        VkPipeline particle_pipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout particle_pipeline_layout_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout particle_descriptor_layout_ = VK_NULL_HANDLE;
        VkDescriptorPool particle_descriptor_pool_ = VK_NULL_HANDLE;
        VkDescriptorSet particle_descriptor_set_ = VK_NULL_HANDLE;
        bool particle_resources_initialized_ = false;

        bool gbuffer_initialized_ = false;

        /* Cached volume parameters for deferred lighting (set by render_gbuffer_terrain). */
        float deferred_bounds_min_[3] = {0.0f, 0.0f, 0.0f};
        float deferred_bounds_max_[3] = {0.0f, 0.0f, 0.0f};
        float deferred_voxel_size_ = 1.0f;
        int32_t deferred_grid_size_[3] = {0, 0, 0};
        int32_t deferred_total_chunks_ = 0;
        int32_t deferred_chunks_dim_[3] = {0, 0, 0};

        /* GPU profiling */
        static constexpr uint32_t GPU_TIMESTAMP_COUNT = 8;
        VkQueryPool timestamp_query_pool_;
        float timestamp_period_ns_;
        bool timestamps_supported_;
        char gpu_name_[256];

        bool create_timestamp_query_pool();
        void destroy_timestamp_query_pool();

        bool create_gbuffer_resources();
        bool create_gbuffer_render_pass();
        bool create_gbuffer_pipeline();
        bool create_deferred_lighting_pipeline();
        bool create_gbuffer_descriptor_sets();
        bool create_deferred_lighting_descriptor_sets();
        void update_deferred_shadow_buffer_descriptor(uint32_t frame_index, VkImageView shadow_view);
        void destroy_gbuffer_resources();
        bool init_deferred_pipeline();
        bool init_deferred_descriptors();

        bool create_shadow_volume_resources(uint32_t width, uint32_t height, uint32_t depth);
        void destroy_shadow_volume_resources();
        void upload_shadow_volume(const uint8_t *mip0, uint32_t w0, uint32_t h0, uint32_t d0,
                                  const uint8_t *mip1, uint32_t w1, uint32_t h1, uint32_t d1,
                                  const uint8_t *mip2, uint32_t w2, uint32_t h2, uint32_t d2);

        bool create_blue_noise_texture();
        void destroy_blue_noise_texture();

        bool create_motion_vector_resources();
        void destroy_motion_vector_resources();

        bool create_vobj_atlas_resources(uint32_t max_objects);
        bool create_vobj_pipeline();
        bool create_vobj_descriptor_sets();
        void upload_vobj_to_atlas(uint32_t object_index, const VoxelObject *obj);
        void upload_vobj_metadata(const VoxelObjectWorld *world);

        /* Unified raymarching compute infrastructure */
        bool init_compute_raymarching();
        bool create_gbuffer_compute_pipeline();
        bool create_shadow_compute_pipeline();
        bool create_gbuffer_compute_descriptor_sets();
        bool create_shadow_compute_descriptor_sets();
        void update_shadow_volume_descriptor();
        bool create_shadow_output_resources();
        bool create_shadow_history_resources();
        bool create_temporal_shadow_pipeline();
        bool create_temporal_shadow_descriptor_sets();
        void destroy_compute_raymarching_resources();
        void dispatch_gbuffer_compute(const VoxelVolume *vol);
        void dispatch_shadow_compute();
        void dispatch_temporal_shadow_resolve();

        /* Raymarched particle rendering */
        bool init_particle_resources();
        bool create_particle_pipeline();
        void destroy_particle_resources();

    public:
        struct GPUTimings
        {
            float shadow_pass_ms;
            float main_pass_ms;
            float total_gpu_ms;
        };

        bool get_gpu_timings(GPUTimings *out_timings) const;
        bool is_gpu_profiling_supported() const { return timestamps_supported_; }
        const char *get_gpu_name() const { return gpu_name_; }
    };

}

#endif
