#ifndef PATCH_ENGINE_RENDERER_H
#define PATCH_ENGINE_RENDERER_H

#include <vulkan/vulkan.h>
#include "../core/types.h"
#include "../core/math.h"
#include "../core/particles.h"
#include "../core/voxel_object.h"
#include "../game/humanoid.h"
#include "window.h"
#include <cstdint>
#include <vector>

namespace patch {

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct MeshBuffers {
    VulkanBuffer vertex;
    VulkanBuffer index;
    uint32_t index_count;
};

class Renderer {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void begin_frame(uint32_t* image_index);
    void begin_shadow_pass();
    void end_shadow_pass();
    void begin_main_pass(uint32_t image_index);
    void end_frame(uint32_t image_index);

    void draw_ball(const Ball* ball);
    void draw_voxel_object(const VoxelObject* obj);
    void draw_humanoid_voxels(Vec3 base_pos, const HumanoidModel* model, const HumanoidPose* pose, Vec3 color);
    void draw_humanoid_ragdoll(const HumanoidModel* model, Vec3 color);
    void draw_pit(const Bounds3D& bounds);
    void draw_voxels(const Voxel* voxels, const Bounds3D& bounds, float voxel_size);
    void draw_particle(const Particle* particle);
    void draw_particles(const ParticleSystem* sys);

    void draw_box(Vec3 center, Vec3 scale, Vec3 color, float alpha = 1.0f);
    void draw_box_rotated(Vec3 center, Vec3 scale, Vec3 rotation, Vec3 color, float alpha = 1.0f);
    void draw_box_pivoted(Vec3 pivot, Vec3 offset, Vec3 scale, Vec3 rotation, Vec3 color, float alpha = 1.0f);
    void draw_sphere(Vec3 center, float radius, Vec3 color, float alpha = 1.0f);

    void draw_controls_overlay();
    void draw_bricked_text(float survival_time_seconds, int32_t destroyed_cubes);

    void begin_ui();
    void end_ui();
    void draw_ui_quad(float cx, float cy, float w, float h, Vec3 color, float alpha);
    void draw_ui_text(float x, float y, float pixel, Vec3 color, float alpha, const char* text);

    void set_orthographic(float width, float height, float depth);
    void set_view_angle(float yaw_degrees, float distance);
    void set_view_angle_at(float yaw_degrees, float distance, Vec3 target);
    void set_view_angle_at(float yaw_degrees, float distance, Vec3 target, float dt);
    void set_look_at(Vec3 eye, Vec3 target);
    void set_look_at(Vec3 eye, Vec3 target, float dt);
    void on_resize();
    bool screen_to_world_floor(float screen_x, float screen_y, float floor_y, Vec3* out_world) const;
    void screen_to_ray(float screen_x, float screen_y, Vec3* out_origin, Vec3* out_dir) const;

    Vec3 get_camera_position() const { return camera_position_; }

private:
    bool create_instance();
    bool select_physical_device();
    bool find_queue_families();
    bool create_logical_device();
    bool create_swapchain();
    bool create_render_pass();
    bool create_depth_resources();
    bool create_shadow_resources();
    bool create_framebuffers();
    bool create_command_pool();
    bool create_sync_objects();
    bool recreate_swapchain();
    void destroy_swapchain_objects();
    bool create_pipeline(const char* vert_path, const char* frag_path,
                         bool enable_blend, bool depth_write,
                         VkCullModeFlags cull_mode, VkPipeline* out_pipeline);
    bool create_pipelines();
    bool create_shadow_pipeline();
    bool create_voxel_pipeline();
    bool create_voxel_resources();

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties, VulkanBuffer* buffer);
    void destroy_buffer(VulkanBuffer* buffer);
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

    void create_sphere_mesh(int sectors, int stacks);
    void create_quad_mesh();
    void create_box_mesh();

    void draw_box_internal(Vec3 center, Vec3 scale, Vec3 color, float alpha);

    void cleanup();

    void update_shadow_uniforms();

    Window& window_;

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
    VkRenderPass shadow_render_pass_;
    VkPipelineLayout pipeline_layout_;
    VkPipeline ball_pipeline_;
    VkPipeline shadow_pipeline_;
    VkPipeline ui_pipeline_;

    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    VkDescriptorSetLayout shadow_descriptor_layout_;
    VkDescriptorPool shadow_descriptor_pool_;
    VkDescriptorSet shadow_descriptor_sets_[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer shadow_ubo_[MAX_FRAMES_IN_FLIGHT];
    VkSampler shadow_sampler_;
    VkImage shadow_image_[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory shadow_image_memory_[MAX_FRAMES_IN_FLIGHT];
    VkImageView shadow_image_view_[MAX_FRAMES_IN_FLIGHT];
    VkFramebuffer shadow_framebuffer_[MAX_FRAMES_IN_FLIGHT];
    VkImageLayout shadow_image_layout_[MAX_FRAMES_IN_FLIGHT];

    bool shadow_pass_active_;
    Vec3 camera_target_;
    bool camera_initialized_;

    VkDescriptorSetLayout voxel_descriptor_layout_;
    VkDescriptorPool voxel_descriptor_pool_;
    VkDescriptorSet voxel_descriptor_sets_[MAX_FRAMES_IN_FLIGHT];
    VkPipelineLayout voxel_pipeline_layout_;
    VkPipeline voxel_pipeline_;
    VulkanBuffer voxel_ssbo_[MAX_FRAMES_IN_FLIGHT];

    VkCommandPool command_pool_;
    VkCommandBuffer command_buffers_[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore image_available_semaphores_[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished_semaphores_[MAX_FRAMES_IN_FLIGHT];
    VkFence in_flight_fences_[MAX_FRAMES_IN_FLIGHT];

    uint32_t current_frame_;

    VkImage depth_image_;
    VkDeviceMemory depth_image_memory_;
    VkImageView depth_image_view_;

    MeshBuffers sphere_mesh_;
    MeshBuffers quad_mesh_;
    MeshBuffers box_mesh_;

    Mat4 view_matrix_;
    Mat4 projection_matrix_;
    float ortho_base_width_;
    float ortho_base_height_;
    float ortho_base_depth_;
    float ortho_half_width_;
    float ortho_half_height_;
    Vec3 camera_position_;
};

}

#endif
