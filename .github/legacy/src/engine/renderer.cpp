#include "renderer.h"
#include "ui_font.h"
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace patch {

namespace {

static char* read_file(const char* filename, size_t* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return nullptr;
    }
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = static_cast<char*>(malloc(*size));
    if (!buffer) {
        fclose(file);
        return nullptr;
    }
    fread(buffer, 1, *size, file);
    fclose(file);
    return buffer;
}

static Mat4 mat4_translate_scale_clip(float center_x, float center_y, float sx, float sy) {
    Mat4 t = mat4_translation(vec3_create(center_x, center_y, 0.0f));
    Mat4 s = mat4_scaling(vec3_create(sx, sy, 1.0f));
    return mat4_multiply(t, s);
}

}

static void cmd_set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent);

Renderer::Renderer(Window& window)
    : window_(window)
    , instance_(VK_NULL_HANDLE)
    , physical_device_(VK_NULL_HANDLE)
    , device_(VK_NULL_HANDLE)
    , graphics_queue_(VK_NULL_HANDLE)
    , present_queue_(VK_NULL_HANDLE)
    , graphics_family_(UINT32_MAX)
    , present_family_(UINT32_MAX)
    , surface_(VK_NULL_HANDLE)
    , swapchain_(VK_NULL_HANDLE)
    , swapchain_format_(VK_FORMAT_UNDEFINED)
    , swapchain_extent_{}
    , render_pass_(VK_NULL_HANDLE)
    , shadow_render_pass_(VK_NULL_HANDLE)
    , pipeline_layout_(VK_NULL_HANDLE)
    , ball_pipeline_(VK_NULL_HANDLE)
    , shadow_pipeline_(VK_NULL_HANDLE)
    , ui_pipeline_(VK_NULL_HANDLE)
    , shadow_descriptor_layout_(VK_NULL_HANDLE)
    , shadow_descriptor_pool_(VK_NULL_HANDLE)
    , shadow_sampler_(VK_NULL_HANDLE)
    , shadow_pass_active_(false)
    , camera_target_(vec3_zero())
    , camera_initialized_(false)
    , voxel_descriptor_layout_(VK_NULL_HANDLE)
    , voxel_descriptor_pool_(VK_NULL_HANDLE)
    , voxel_pipeline_layout_(VK_NULL_HANDLE)
    , voxel_pipeline_(VK_NULL_HANDLE)
    , command_pool_(VK_NULL_HANDLE)
    , current_frame_(0)
    , depth_image_(VK_NULL_HANDLE)
    , depth_image_memory_(VK_NULL_HANDLE)
    , depth_image_view_(VK_NULL_HANDLE)
    , view_matrix_(mat4_identity())
    , projection_matrix_(mat4_identity())
    , ortho_base_width_(0.0f)
    , ortho_base_height_(0.0f)
    , ortho_base_depth_(0.0f)
    , ortho_half_width_(0.0f)
    , ortho_half_height_(0.0f)
    , camera_position_(vec3_zero()) {

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        voxel_descriptor_sets_[i] = VK_NULL_HANDLE;
        voxel_ssbo_[i].buffer = VK_NULL_HANDLE;
        voxel_ssbo_[i].memory = VK_NULL_HANDLE;

        shadow_descriptor_sets_[i] = VK_NULL_HANDLE;
        shadow_ubo_[i].buffer = VK_NULL_HANDLE;
        shadow_ubo_[i].memory = VK_NULL_HANDLE;
        shadow_image_[i] = VK_NULL_HANDLE;
        shadow_image_memory_[i] = VK_NULL_HANDLE;
        shadow_image_view_[i] = VK_NULL_HANDLE;
        shadow_framebuffer_[i] = VK_NULL_HANDLE;
        shadow_image_layout_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (!create_instance()) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    surface_ = window_.create_surface(instance_);
    if (surface_ == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    if (!select_physical_device()) {
        throw std::runtime_error("Failed to select physical device");
    }

    if (!find_queue_families()) {
        throw std::runtime_error("Failed to find queue families");
    }

    if (!create_logical_device()) {
        throw std::runtime_error("Failed to create logical device");
    }

    if (!create_swapchain()) {
        throw std::runtime_error("Failed to create swapchain");
    }

    if (!create_render_pass()) {
        throw std::runtime_error("Failed to create render pass");
    }

    if (!create_depth_resources()) {
        throw std::runtime_error("Failed to create depth resources");
    }

    if (!create_shadow_resources()) {
        throw std::runtime_error("Failed to create shadow resources");
    }

    if (!create_pipelines()) {
        throw std::runtime_error("Failed to create pipelines");
    }

    if (!create_voxel_resources()) {
        throw std::runtime_error("Failed to create voxel resources");
    }

    if (!create_voxel_pipeline()) {
        throw std::runtime_error("Failed to create voxel pipeline");
    }

    if (!create_framebuffers()) {
        throw std::runtime_error("Failed to create framebuffers");
    }

    if (!create_command_pool()) {
        throw std::runtime_error("Failed to create command pool");
    }

    if (!create_sync_objects()) {
        throw std::runtime_error("Failed to create sync objects");
    }

    create_sphere_mesh(32, 16);
    create_quad_mesh();
    create_box_mesh();

    float iso_distance = 30.0f;
    float iso_yaw = 45.0f * K_DEG_TO_RAD;
    float iso_pitch = 35.26f * K_DEG_TO_RAD;

    Vec3 eye = vec3_create(
        iso_distance * sinf(iso_yaw) * cosf(iso_pitch),
        iso_distance * sinf(iso_pitch),
        iso_distance * cosf(iso_yaw) * cosf(iso_pitch)
    );

    view_matrix_ = mat4_look_at(eye, vec3_zero(), vec3_create(0.0f, 1.0f, 0.0f));

    float aspect = window_.aspect_ratio();
    float ortho_size = 10.0f;
    projection_matrix_ = mat4_ortho(-ortho_size * aspect, ortho_size * aspect, -ortho_size, ortho_size, 0.1f, 100.0f);
    ortho_half_width_ = ortho_size * aspect;
    ortho_half_height_ = ortho_size;
}

Renderer::~Renderer() {
    cleanup();
}

void Renderer::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);

        destroy_buffer(&sphere_mesh_.vertex);
        destroy_buffer(&sphere_mesh_.index);
        destroy_buffer(&quad_mesh_.vertex);
        destroy_buffer(&quad_mesh_.index);
        destroy_buffer(&box_mesh_.vertex);
        destroy_buffer(&box_mesh_.index);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            destroy_buffer(&voxel_ssbo_[i]);
            destroy_buffer(&shadow_ubo_[i]);
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (shadow_framebuffer_[i]) vkDestroyFramebuffer(device_, shadow_framebuffer_[i], nullptr);
            if (shadow_image_view_[i]) vkDestroyImageView(device_, shadow_image_view_[i], nullptr);
            if (shadow_image_[i]) vkDestroyImage(device_, shadow_image_[i], nullptr);
            if (shadow_image_memory_[i]) vkFreeMemory(device_, shadow_image_memory_[i], nullptr);
        }

        if (shadow_sampler_) vkDestroySampler(device_, shadow_sampler_, nullptr);
        if (shadow_descriptor_pool_) vkDestroyDescriptorPool(device_, shadow_descriptor_pool_, nullptr);
        if (shadow_descriptor_layout_) vkDestroyDescriptorSetLayout(device_, shadow_descriptor_layout_, nullptr);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (image_available_semaphores_[i]) vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
            if (render_finished_semaphores_[i]) vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
            if (in_flight_fences_[i]) vkDestroyFence(device_, in_flight_fences_[i], nullptr);
        }

        if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);

        for (auto fb : framebuffers_) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }

        if (depth_image_view_) vkDestroyImageView(device_, depth_image_view_, nullptr);
        if (depth_image_) vkDestroyImage(device_, depth_image_, nullptr);
        if (depth_image_memory_) vkFreeMemory(device_, depth_image_memory_, nullptr);

        if (ball_pipeline_) vkDestroyPipeline(device_, ball_pipeline_, nullptr);
        if (shadow_pipeline_) vkDestroyPipeline(device_, shadow_pipeline_, nullptr);
        if (ui_pipeline_) vkDestroyPipeline(device_, ui_pipeline_, nullptr);
        if (voxel_pipeline_) vkDestroyPipeline(device_, voxel_pipeline_, nullptr);
        if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        if (voxel_pipeline_layout_) vkDestroyPipelineLayout(device_, voxel_pipeline_layout_, nullptr);
        if (voxel_descriptor_pool_) vkDestroyDescriptorPool(device_, voxel_descriptor_pool_, nullptr);
        if (voxel_descriptor_layout_) vkDestroyDescriptorSetLayout(device_, voxel_descriptor_layout_, nullptr);
        if (shadow_render_pass_) vkDestroyRenderPass(device_, shadow_render_pass_, nullptr);
        if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);

        for (auto view : swapchain_image_views_) {
            vkDestroyImageView(device_, view, nullptr);
        }

        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);

        vkDestroyDevice(device_, nullptr);
    }

    if (surface_ && instance_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

void Renderer::begin_frame(uint32_t* image_index) {
    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

    vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_semaphores_[current_frame_], VK_NULL_HANDLE, image_index);
    vkResetCommandBuffer(command_buffers_[current_frame_], 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(command_buffers_[current_frame_], &begin_info);
    shadow_pass_active_ = false;
}

void Renderer::begin_shadow_pass() {
    update_shadow_uniforms();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = shadow_image_layout_[current_frame_];
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadow_image_[current_frame_];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    if (barrier.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    vkCmdPipelineBarrier(command_buffers_[current_frame_],
                         src_stage, dst_stage,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    shadow_image_layout_[current_frame_] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkClearValue clear_value{};
    clear_value.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = shadow_render_pass_;
    rp.framebuffer = shadow_framebuffer_[current_frame_];
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
    rp.clearValueCount = 1;
    rp.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command_buffers_[current_frame_], &rp, VK_SUBPASS_CONTENTS_INLINE);
    cmd_set_viewport_scissor(command_buffers_[current_frame_], { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE });
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);

    shadow_pass_active_ = true;
}

void Renderer::end_shadow_pass() {
    vkCmdEndRenderPass(command_buffers_[current_frame_]);
    shadow_image_layout_[current_frame_] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadow_pass_active_ = false;
}

void Renderer::begin_main_pass(uint32_t image_index) {
    VkClearValue clear_values[2];
    clear_values[0].color = { { 0.68f, 0.85f, 0.92f, 1.0f } };
    clear_values[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = framebuffers_[image_index];
    render_pass_info.renderArea.offset = { 0, 0 };
    render_pass_info.renderArea.extent = swapchain_extent_;
    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffers_[current_frame_], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    cmd_set_viewport_scissor(command_buffers_[current_frame_], swapchain_extent_);
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ball_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);

    VkBuffer vertex_buffers[] = { sphere_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Renderer::end_frame(uint32_t image_index) {
    vkCmdEndRenderPass(command_buffers_[current_frame_]);
    vkEndCommandBuffer(command_buffers_[current_frame_]);

    VkSemaphore wait_semaphores[] = { image_available_semaphores_[current_frame_] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signal_semaphores[] = { render_finished_semaphores_[current_frame_] };

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

    VkSwapchainKHR swapchains[] = { swapchain_ };

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    vkQueuePresentKHR(present_queue_, &present_info);
    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::draw_ball(const Ball* ball) {
    VkBuffer vertex_buffers[] = { sphere_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    Mat4 translation = mat4_translation(ball->position);
    Mat4 scale = mat4_scaling(vec3_create(ball->radius, ball->radius, ball->radius));
    Mat4 model = mat4_multiply(translation, scale);

    PushConstants pc;
    pc.model = model;
    pc.view = view_matrix_;
    pc.projection = projection_matrix_;
    pc.color_alpha = { ball->color.x, ball->color.y, ball->color.z, 1.0f };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    vkCmdDrawIndexed(command_buffers_[current_frame_], sphere_mesh_.index_count, 1, 0, 0, 0);
}

void Renderer::draw_particle(const Particle* particle) {
    if (shadow_pass_active_) return;
    if (!particle->active) return;

    Mat4 translation = mat4_translation(particle->position);
    Mat4 rotation = mat4_rotation_euler(particle->rotation);
    float s = particle->radius * 2.0f;
    Mat4 scale = mat4_scaling(vec3_create(s, s, s));
    Mat4 model = mat4_multiply(mat4_multiply(translation, rotation), scale);

    PushConstants pc;
    pc.model = model;
    pc.view = view_matrix_;
    pc.projection = projection_matrix_;
    pc.color_alpha = { particle->color.x, particle->color.y, particle->color.z, 1.0f };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);
}

void Renderer::draw_particles(const ParticleSystem* sys) {
    if (shadow_pass_active_ || !sys) return;
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ball_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);

    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    for (int32_t i = 0; i < sys->count; i++) {
        draw_particle(&sys->particles[i]);
    }

    VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Renderer::draw_controls_overlay() {
    if (shadow_pass_active_) return;
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);

    VkBuffer vertex_buffers[] = { quad_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], quad_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    Mat4 view = mat4_identity();
    Mat4 proj = mat4_identity();

    auto draw_quad = [&](float cx, float cy, float sx, float sy, Vec3 color, float alpha) {
        PushConstants pc;
        pc.model = mat4_translate_scale_clip(cx, cy, sx, sy);
        pc.view = view;
        pc.projection = proj;
        pc.color_alpha = { color.x, color.y, color.z, alpha };
        pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };
        vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(command_buffers_[current_frame_], quad_mesh_.index_count, 1, 0, 0, 0);
    };

    auto draw_text = [&](float x_left, float y_top, float pixel, Vec3 color, float alpha, const char* text) {
        float x = x_left;
        for (const char* p = text; *p; ++p) {
            uint8_t rows[7];
            font5x7_rows(rows, *p);
            for (int ry = 0; ry < 7; ry++) {
                uint8_t bits = rows[6 - ry];
                for (int rx = 0; rx < 5; rx++) {
                    if (bits & (1u << (4 - rx))) {
                        float cx = x + (float)rx * pixel + pixel * 0.5f;
                        float cy = y_top - (float)ry * pixel - pixel * 0.5f;
                        draw_quad(cx, cy, pixel, pixel, color, alpha);
                    }
                }
            }
            x += pixel * 6.0f;
        }
    };

    Vec3 text_color = vec3_create(0.3f, 0.7f, 0.55f);

    float panel_left = -0.98f;
    float panel_top = 0.96f;

    float pad_x = 0.03f;
    float pad_y = 0.04f;
    float pixel = 0.005f;

    float x = panel_left + pad_x;
    float y = panel_top - pad_y;

    draw_text(x, y, pixel, text_color, 0.95f, "CONTROLS");

    y -= pixel * 9.0f;
    draw_text(x, y, pixel, text_color, 0.92f, "LMB: DESTROY VOXELS");

    y -= pixel * 9.0f;
    draw_text(x, y, pixel, text_color, 0.92f, "HOVER+MOVE: PUSH OBJECTS");

    y -= pixel * 9.0f;
    draw_text(x, y, pixel, text_color, 0.92f, "FRAGMENTS: MAX 500");

    y -= pixel * 9.0f;
    draw_text(x, y, pixel, text_color, 0.92f, "ESC: QUIT");

    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ball_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);
    VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Renderer::draw_bricked_text(float survival_time_seconds, int32_t destroyed_cubes) {
    if (shadow_pass_active_) {
        return;
    }
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);

    VkBuffer vertex_buffers[] = { quad_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], quad_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    auto draw_quad = [&](float cx, float cy, float w, float h, Vec3 color, float alpha) {
        Mat4 xform = mat4_translate_scale_clip(cx, -cy, w, h);
        PushConstants pc;
        pc.model = xform;
        pc.view = mat4_identity();
        pc.projection = mat4_identity();
        pc.color_alpha = { color.x, color.y, color.z, alpha };
        pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };
        vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);
        vkCmdDrawIndexed(command_buffers_[current_frame_], quad_mesh_.index_count, 1, 0, 0, 0);
    };

    auto draw_text_centered = [&](float cx, float cy, float pixel, Vec3 color, float alpha, const char* text) {
        size_t len = strlen(text);
        float total_width = (float)len * pixel * 6.0f - pixel;
        float text_height = pixel * 7.0f;
        float x = cx - total_width * 0.5f;
        float y_top = cy + text_height * 0.5f;
        for (const char* p = text; *p; ++p) {
            uint8_t rows[7];
            font5x7_rows(rows, *p);
            for (int ry = 0; ry < 7; ry++) {
                uint8_t bits = rows[ry];
                for (int rx = 0; rx < 5; rx++) {
                    if (bits & (1u << (4 - rx))) {
                        float qx = x + (float)rx * pixel + pixel * 0.5f;
                        float qy = y_top - (float)ry * pixel - pixel * 0.5f;
                        draw_quad(qx, qy, pixel, pixel, color, alpha);
                    }
                }
            }
            x += pixel * 6.0f;
        }
    };

    auto text_width = [&](const char* text, float pixel) {
        size_t len = strlen(text);
        return (float)len * pixel * 6.0f - pixel;
    };

    const char* title = "BRICKED";
    const char* hint = "PRESS R TO RESTART";

    char time_text[64];
    snprintf(time_text, sizeof(time_text), "SURVIVED %.1fs", survival_time_seconds);

    char destroyed_text[64];
    snprintf(destroyed_text, sizeof(destroyed_text), "DESTROYED %d", destroyed_cubes);


    float min_dim = (float)((window_.width() < window_.height()) ? window_.width() : window_.height());
    float ndc_per_screen_px = 2.0f / min_dim;

    float title_h_px = clampf(min_dim * 0.10f, 44.0f, 78.0f);
    float body_h_px = clampf(title_h_px * 0.55f, 24.0f, 44.0f);
    float hint_h_px = clampf(title_h_px * 0.45f, 20.0f, 38.0f);

    float px_title = ndc_per_screen_px * (title_h_px / 7.0f);
    float px_body = ndc_per_screen_px * (body_h_px / 7.0f);
    float px_hint = ndc_per_screen_px * (hint_h_px / 7.0f);

    float h_title = px_title * 7.0f;
    float h_body = px_body * 7.0f;
    float h_hint = px_hint * 7.0f;
    float gap1 = px_body * 9.0f;
    float gap_stats = px_body * 5.0f;
    float gap2 = px_body * 8.0f;
    float margin_x = px_body * 16.0f;
    float margin_y = px_body * 16.0f;

    float w_title = text_width(title, px_title);
    float w_body = text_width(time_text, px_body);
    float w_destroyed = text_width(destroyed_text, px_body);
    float w_hint = text_width(hint, px_hint);
    float content_w = fmaxf(w_title, fmaxf(w_body, fmaxf(w_destroyed, w_hint)));
    float content_h = h_title + gap1 + h_body + gap_stats + h_body + gap2 + h_hint;

    float panel_w = content_w + margin_x * 2.0f;
    float panel_h = content_h + margin_y * 2.0f;

    Vec3 panel_color = vec3_create(0.02f, 0.04f, 0.06f);
    draw_quad(0.0f, 0.0f, panel_w, panel_h, panel_color, 0.78f);

    Vec3 border_color = vec3_create(0.9f, 0.25f, 0.25f);
    float border_t = px_body * 1.6f;
    draw_quad(0.0f, panel_h * 0.5f, panel_w, border_t, border_color, 0.9f);
    draw_quad(0.0f, -panel_h * 0.5f, panel_w, border_t, border_color, 0.9f);
    draw_quad(-panel_w * 0.5f, 0.0f, border_t, panel_h, border_color, 0.9f);
    draw_quad(panel_w * 0.5f, 0.0f, border_t, panel_h, border_color, 0.9f);

    float cy_title = (content_h * 0.5f) - (h_title * 0.5f);
    float cy_time = cy_title - (h_title * 0.5f + gap1 + h_body * 0.5f);
    float cy_destroyed = cy_time - (h_body * 0.5f + gap_stats + h_body * 0.5f);
    float cy_hint = cy_destroyed - (h_body * 0.5f + gap2 + h_hint * 0.5f);

    Vec3 bricked_color = vec3_create(0.95f, 0.25f, 0.25f);
    draw_text_centered(0.0f, cy_title, px_title, bricked_color, 1.0f, title);

    Vec3 time_color = vec3_create(0.98f, 0.85f, 0.45f);
    draw_text_centered(0.0f, cy_time, px_body, time_color, 1.0f, time_text);

    Vec3 stat_color = vec3_create(0.92f, 0.92f, 0.92f);
    draw_text_centered(0.0f, cy_destroyed, px_body, stat_color, 1.0f, destroyed_text);

    Vec3 restart_color = vec3_create(0.4f, 0.85f, 0.65f);
    draw_text_centered(0.0f, cy_hint, px_hint, restart_color, 0.95f, hint);

    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ball_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);
    VkBuffer sphere_buffers2[] = { sphere_mesh_.vertex.buffer };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers2, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Renderer::begin_ui() {
    if (shadow_pass_active_) {
        return;
    }
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);

    VkBuffer vertex_buffers[] = { quad_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], quad_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Renderer::end_ui() {
    if (shadow_pass_active_) {
        return;
    }
    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ball_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);
    VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Renderer::draw_ui_quad(float cx, float cy, float w, float h, Vec3 color, float alpha) {
    if (shadow_pass_active_) {
        return;
    }
    Mat4 view = mat4_identity();
    Mat4 proj = mat4_identity();

    PushConstants pc;
    pc.model = mat4_translate_scale_clip(cx, -cy, w, h);
    pc.view = view;
    pc.projection = proj;
    pc.color_alpha = { color.x, color.y, color.z, alpha };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };
    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);
    vkCmdDrawIndexed(command_buffers_[current_frame_], quad_mesh_.index_count, 1, 0, 0, 0);
}

void Renderer::draw_ui_text(float x_left, float y_top, float pixel, Vec3 color, float alpha, const char* text) {
    if (shadow_pass_active_) {
        return;
    }
    Mat4 view = mat4_identity();
    Mat4 proj = mat4_identity();

    float x = x_left;
    for (const char* p = text; *p; ++p) {
        uint8_t rows[7];
        font5x7_rows(rows, *p);
        for (int ry = 0; ry < 7; ry++) {
            uint8_t bits = rows[ry];
            for (int rx = 0; rx < 5; rx++) {
                if (bits & (1u << (4 - rx))) {
                    float cx = x + (float)rx * pixel + pixel * 0.5f;
                    float cy = y_top - (float)ry * pixel - pixel * 0.5f;

                    PushConstants pc;
                    pc.model = mat4_translate_scale_clip(cx, -cy, pixel, pixel);
                    pc.view = view;
                    pc.projection = proj;
                    pc.color_alpha = { color.x, color.y, color.z, alpha };
                    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };
                    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(PushConstants), &pc);
                    vkCmdDrawIndexed(command_buffers_[current_frame_], quad_mesh_.index_count, 1, 0, 0, 0);
                }
            }
        }
        x += pixel * 6.0f;
    }
}

void Renderer::draw_voxel_object(const VoxelObject* obj) {
    if (!obj->active || obj->voxel_count == 0) return;

    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    float voxel_render_size = obj->voxel_size;
    
    Vec3 pivot = vec3_add(obj->position, obj->shape_center_offset);
    Mat4 rotation = mat4_rotation_euler(obj->rotation);

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                int32_t idx = vobj_index(x, y, z);
                if (!obj->voxels[idx].active) continue;

                Vec3 local_pos;
                local_pos.x = ((float)x + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.x;
                local_pos.y = ((float)y + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.y;
                local_pos.z = ((float)z + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.z;
                
                Vec3 rotated = mat4_transform_point(rotation, local_pos);
                Vec3 voxel_pos = vec3_add(pivot, rotated);

                Mat4 translation = mat4_translation(voxel_pos);
                Mat4 scale = mat4_scaling(vec3_create(voxel_render_size, voxel_render_size, voxel_render_size));
                Mat4 voxel_rot = rotation;
                Mat4 model = mat4_multiply(mat4_multiply(translation, voxel_rot), scale);

                Vec3 color = vec3_create(
                    (float)obj->voxels[idx].r / 255.0f,
                    (float)obj->voxels[idx].g / 255.0f,
                    (float)obj->voxels[idx].b / 255.0f
                );

                PushConstants pc;
                pc.model = model;
                pc.view = view_matrix_;
                pc.projection = projection_matrix_;
                pc.color_alpha = { color.x, color.y, color.z, 1.0f };
                pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

                vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);

                vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);
            }
        }
    }

    if (!shadow_pass_active_) {
        VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

static Vec3 get_humanoid_part_color(HumanoidPart part, Vec3 base_color) {
    switch (part) {
        case HUMANOID_PART_HEAD:
            return vec3_create(0.95f, 0.70f, 0.65f);
        case HUMANOID_PART_BODY:
            return base_color;
        case HUMANOID_PART_ARM_LEFT:
        case HUMANOID_PART_ARM_RIGHT:
            return vec3_create(0.95f, 0.70f, 0.65f);
        case HUMANOID_PART_LEG_LEFT:
        case HUMANOID_PART_LEG_RIGHT:
            return vec3_create(base_color.x * 0.85f, base_color.y * 0.85f, base_color.z * 0.85f);
        default:
            return base_color;
    }
}

void Renderer::draw_humanoid_voxels(Vec3 base_pos, const HumanoidModel* model, const HumanoidPose* pose, Vec3 color) {
    if (model->voxel_count == 0) return;

    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    float render_size = HUMANOID_VOXEL_SIZE;

    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active) continue;

        Vec3 voxel_pos = humanoid_transform_voxel(&model->voxels[i], base_pos, &model->dims, pose);

        float swing = 0.0f;
        switch (model->voxels[i].part) {
            case HUMANOID_PART_ARM_LEFT:
                swing = pose->arm_swing;
                break;
            case HUMANOID_PART_ARM_RIGHT:
                swing = -pose->arm_swing - pose->punch_swing;
                break;
            case HUMANOID_PART_LEG_LEFT:
                swing = pose->leg_swing;
                break;
            case HUMANOID_PART_LEG_RIGHT:
                swing = -pose->leg_swing;
                break;
            default:
                break;
        }
        Vec3 voxel_rotation = vec3_create(swing, -pose->yaw, 0.0f);

        Vec3 voxel_color = model->voxels[i].has_color_override 
            ? model->voxels[i].color_override 
            : get_humanoid_part_color(model->voxels[i].part, color);

        Mat4 translation = mat4_translation(voxel_pos);
        Mat4 rotation = mat4_rotation_euler(voxel_rotation);
        Mat4 scale = mat4_scaling(vec3_create(render_size, render_size, render_size));
        Mat4 xform = mat4_multiply(mat4_multiply(translation, rotation), scale);

        PushConstants pc;
        pc.model = xform;
        pc.view = view_matrix_;
        pc.projection = projection_matrix_;
        pc.color_alpha = { voxel_color.x, voxel_color.y, voxel_color.z, 1.0f };
        pc.params = { 1.0f, 0.0f, 0.0f, 0.0f };

        vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);

        vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);
    }

    if (!shadow_pass_active_) {
        VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void Renderer::draw_humanoid_ragdoll(const HumanoidModel* model, Vec3 color) {
    if (model->voxel_count == 0 || !model->ragdoll.ragdoll_active) return;

    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    float render_size = HUMANOID_VOXEL_SIZE;

    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active) continue;

        Vec3 voxel_pos = humanoid_transform_voxel_ragdoll(&model->voxels[i], model);
        
        const RagdollLimb* limb = nullptr;
        switch (model->voxels[i].part) {
            case HUMANOID_PART_HEAD: limb = &model->ragdoll.head; break;
            case HUMANOID_PART_BODY: limb = &model->ragdoll.torso; break;
            case HUMANOID_PART_ARM_LEFT: limb = &model->ragdoll.arm_left; break;
            case HUMANOID_PART_ARM_RIGHT: limb = &model->ragdoll.arm_right; break;
            case HUMANOID_PART_LEG_LEFT: limb = &model->ragdoll.leg_left; break;
            case HUMANOID_PART_LEG_RIGHT: limb = &model->ragdoll.leg_right; break;
        }
        
        Vec3 voxel_rotation = vec3_zero();
        if (limb) {
            voxel_rotation = vec3_add(limb->rotation, model->ragdoll.rotation);
        }
        
        Vec3 voxel_color = model->voxels[i].has_color_override 
            ? model->voxels[i].color_override 
            : get_humanoid_part_color(model->voxels[i].part, color);

        Mat4 translation = mat4_translation(voxel_pos);
        Mat4 rotation = mat4_rotation_euler(voxel_rotation);
        Mat4 scale = mat4_scaling(vec3_create(render_size, render_size, render_size));
        Mat4 xform = mat4_multiply(mat4_multiply(translation, rotation), scale);

        PushConstants pc;
        pc.model = xform;
        pc.view = view_matrix_;
        pc.projection = projection_matrix_;
        pc.color_alpha = { voxel_color.x, voxel_color.y, voxel_color.z, 1.0f };
        pc.params = { 1.0f, 0.0f, 0.0f, 0.0f };

        vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);

        vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);
    }

    if (!shadow_pass_active_) {
        VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void Renderer::draw_box_internal(Vec3 center, Vec3 scale, Vec3 color, float alpha) {
    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    Mat4 translation = mat4_translation(center);
    Mat4 scale_mat = mat4_scaling(scale);
    Mat4 model = mat4_multiply(translation, scale_mat);

    PushConstants pc;
    pc.model = model;
    pc.view = view_matrix_;
    pc.projection = projection_matrix_;
    pc.color_alpha = { color.x, color.y, color.z, alpha };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);

    if (!shadow_pass_active_) {
        VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void Renderer::draw_box(Vec3 center, Vec3 scale, Vec3 color, float alpha) {
    draw_box_internal(center, scale, color, alpha);
}

void Renderer::draw_box_rotated(Vec3 center, Vec3 scale, Vec3 rotation, Vec3 color, float alpha) {
    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    Mat4 translation = mat4_translation(center);
    Mat4 rot = mat4_rotation_euler(rotation);
    Mat4 scale_mat = mat4_scaling(scale);
    Mat4 model = mat4_multiply(mat4_multiply(translation, rot), scale_mat);

    PushConstants pc;
    pc.model = model;
    pc.view = view_matrix_;
    pc.projection = projection_matrix_;
    pc.color_alpha = { color.x, color.y, color.z, alpha };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);

    if (!shadow_pass_active_) {
        VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void Renderer::draw_box_pivoted(Vec3 pivot, Vec3 offset, Vec3 scale, Vec3 rotation, Vec3 color, float alpha) {
    VkBuffer vertex_buffers[] = { box_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], box_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    Mat4 pivot_trans = mat4_translation(pivot);
    Mat4 rot = mat4_rotation_euler(rotation);
    Mat4 offset_trans = mat4_translation(offset);
    Mat4 scale_mat = mat4_scaling(scale);
    Mat4 model = mat4_multiply(mat4_multiply(mat4_multiply(pivot_trans, rot), offset_trans), scale_mat);

    PushConstants pc;
    pc.model = model;
    pc.view = view_matrix_;
    pc.projection = projection_matrix_;
    pc.color_alpha = { color.x, color.y, color.z, alpha };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    vkCmdDrawIndexed(command_buffers_[current_frame_], box_mesh_.index_count, 1, 0, 0, 0);

    if (!shadow_pass_active_) {
        VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
        vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void Renderer::draw_sphere(Vec3 center, float radius, Vec3 color, float alpha) {
    VkBuffer vertex_buffers[] = { sphere_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    Mat4 translation = mat4_translation(center);
    Mat4 scale = mat4_scaling(vec3_create(radius, radius, radius));
    Mat4 model = mat4_multiply(translation, scale);

    PushConstants pc;
    pc.model = model;
    pc.view = view_matrix_;
    pc.projection = projection_matrix_;
    pc.color_alpha = { color.x, color.y, color.z, alpha };
    pc.params = { 0.0f, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(command_buffers_[current_frame_], pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    vkCmdDrawIndexed(command_buffers_[current_frame_], sphere_mesh_.index_count, 1, 0, 0, 0);
}

void Renderer::draw_pit(const Bounds3D& bounds) {
    float width = bounds.max_x - bounds.min_x;
    float height = bounds.max_y - bounds.min_y;
    float depth = bounds.max_z - bounds.min_z;
    float cx = (bounds.min_x + bounds.max_x) * 0.5f;
    float cy = (bounds.min_y + bounds.max_y) * 0.5f;
    float cz = (bounds.min_z + bounds.max_z) * 0.5f;

    Vec3 floor_color = vec3_create(0.95f, 0.85f, 0.82f);
    Vec3 wall_back   = vec3_create(0.65f, 0.82f, 0.85f);
    Vec3 wall_front  = vec3_create(0.95f, 0.75f, 0.80f);
    Vec3 wall_left   = vec3_create(0.95f, 0.72f, 0.78f);
    Vec3 wall_right  = vec3_create(0.60f, 0.80f, 0.82f);

    float wall_thickness = 0.4f;
    float wall_height = height + wall_thickness;

    float cam_dir_x = -view_matrix_.m[2];
    float cam_dir_z = -view_matrix_.m[10];

    draw_box_internal(vec3_create(cx, bounds.min_y - 0.15f, cz),
                      vec3_create(width + wall_thickness * 2.0f, 0.3f, depth + wall_thickness * 2.0f),
                      floor_color, 1.0f);

    if (cam_dir_z < 0.1f) {
        draw_box_internal(vec3_create(cx, cy, bounds.min_z - wall_thickness * 0.5f),
                          vec3_create(width + wall_thickness * 2.0f, wall_height, wall_thickness),
                          wall_back, 1.0f);
    }

    if (cam_dir_z > -0.1f) {
        draw_box_internal(vec3_create(cx, cy, bounds.max_z + wall_thickness * 0.5f),
                          vec3_create(width + wall_thickness * 2.0f, wall_height, wall_thickness),
                          wall_front, 1.0f);
    }

    if (cam_dir_x < 0.1f) {
        draw_box_internal(vec3_create(bounds.min_x - wall_thickness * 0.5f, cy, cz),
                          vec3_create(wall_thickness, wall_height, depth),
                          wall_left, 1.0f);
    }

    if (cam_dir_x > -0.1f) {
        draw_box_internal(vec3_create(bounds.max_x + wall_thickness * 0.5f, cy, cz),
                          vec3_create(wall_thickness, wall_height, depth),
                          wall_right, 1.0f);
    }
}

void Renderer::set_orthographic(float width, float height, float depth) {
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

void Renderer::on_resize() {
    recreate_swapchain();
    if (ortho_base_width_ > 0.0f && ortho_base_height_ > 0.0f && ortho_base_depth_ > 0.0f) {
        set_orthographic(ortho_base_width_, ortho_base_height_, ortho_base_depth_);
    }
}

void Renderer::set_view_angle(float yaw_degrees, float distance) {
    float yaw = yaw_degrees * K_DEG_TO_RAD;
    float tilt = 35.26f * K_DEG_TO_RAD;

    float cos_yaw = cosf(yaw);
    float sin_yaw = sinf(yaw);
    float cos_tilt = cosf(tilt);
    float sin_tilt = sinf(tilt);

    camera_position_ = vec3_create(
        distance * sin_yaw * cos_tilt,
        distance * sin_tilt,
        distance * cos_yaw * cos_tilt
    );

    camera_target_ = vec3_zero();
    camera_initialized_ = true;
    view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
}

void Renderer::set_view_angle_at(float yaw_degrees, float distance, Vec3 target) {
    float yaw = yaw_degrees * K_DEG_TO_RAD;
    float tilt = 35.26f * K_DEG_TO_RAD;

    float cos_yaw = cosf(yaw);
    float sin_yaw = sinf(yaw);
    float cos_tilt = cosf(tilt);
    float sin_tilt = sinf(tilt);

    Vec3 offset = vec3_create(
        distance * sin_yaw * cos_tilt,
        distance * sin_tilt,
        distance * cos_yaw * cos_tilt
    );

    camera_position_ = vec3_add(target, offset);
    camera_target_ = target;
    camera_initialized_ = true;
    view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
}

void Renderer::set_view_angle_at(float yaw_degrees, float distance, Vec3 target, float dt) {
    float yaw = yaw_degrees * K_DEG_TO_RAD;
    float tilt = 35.26f * K_DEG_TO_RAD;

    float cos_yaw = cosf(yaw);
    float sin_yaw = sinf(yaw);
    float cos_tilt = cosf(tilt);
    float sin_tilt = sinf(tilt);

    Vec3 offset = vec3_create(
        distance * sin_yaw * cos_tilt,
        distance * sin_tilt,
        distance * cos_yaw * cos_tilt
    );

    Vec3 desired_position = vec3_add(target, offset);
    Vec3 desired_target = target;

    if (!camera_initialized_ || dt <= 0.0f || dt > 0.25f) {
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

void Renderer::set_look_at(Vec3 eye, Vec3 target) {
    camera_position_ = eye;
    camera_target_ = target;
    camera_initialized_ = true;
    view_matrix_ = mat4_look_at(camera_position_, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
}

void Renderer::set_look_at(Vec3 eye, Vec3 target, float dt) {
    if (!camera_initialized_ || dt <= 0.0f || dt > 0.25f) {
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

bool Renderer::screen_to_world_floor(float screen_x, float screen_y, float floor_y, Vec3* out_world) const {
    if (!out_world) return false;

    float nx = (2.0f * screen_x / static_cast<float>(window_.width())) - 1.0f;
    float ny = 1.0f - (2.0f * screen_y / static_cast<float>(window_.height()));

    Vec3 origin_view = vec3_create(nx * ortho_half_width_, ny * ortho_half_height_, 0.0f);
    Vec3 dir_view = vec3_create(0.0f, 0.0f, -1.0f);

    Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
    Vec3 origin_world = mat4_transform_point(inv_view, origin_view);
    Vec3 dir_world = vec3_normalize(mat4_transform_direction(inv_view, dir_view));

    float denom = dir_world.y;
    if (fabsf(denom) < 1e-5f) {
        return false;
    }

    float t = (floor_y - origin_world.y) / denom;
    *out_world = vec3_add(origin_world, vec3_scale(dir_world, t));
    return true;
}

void Renderer::screen_to_ray(float screen_x, float screen_y, Vec3* out_origin, Vec3* out_dir) const {
    float nx = (2.0f * screen_x / static_cast<float>(window_.width())) - 1.0f;
    float ny = 1.0f - (2.0f * screen_y / static_cast<float>(window_.height()));

    Vec3 origin_view = vec3_create(nx * ortho_half_width_, ny * ortho_half_height_, 0.0f);
    Vec3 dir_view = vec3_create(0.0f, 0.0f, -1.0f);

    Mat4 inv_view = mat4_inverse_rigid(view_matrix_);
    *out_origin = mat4_transform_point(inv_view, origin_view);
    *out_dir = vec3_normalize(mat4_transform_direction(inv_view, dir_view));
}

uint32_t Renderer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

void Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties, VulkanBuffer* buffer) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device_, &buffer_info, nullptr, &buffer->buffer);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, buffer->buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, properties);

    vkAllocateMemory(device_, &alloc_info, nullptr, &buffer->memory);
    vkBindBufferMemory(device_, buffer->buffer, buffer->memory, 0);
}

void Renderer::destroy_buffer(VulkanBuffer* buffer) {
    if (!buffer) return;
    if (buffer->buffer) vkDestroyBuffer(device_, buffer->buffer, nullptr);
    if (buffer->memory) vkFreeMemory(device_, buffer->memory, nullptr);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->memory = VK_NULL_HANDLE;
}

void Renderer::create_sphere_mesh(int sectors, int stacks) {
    int vertex_count = (stacks + 1) * (sectors + 1);
    int index_count_calc = stacks * sectors * 6;

    std::vector<Vertex> vertices(static_cast<size_t>(vertex_count));
    std::vector<uint32_t> indices(static_cast<size_t>(index_count_calc));

    int v = 0;
    for (int i = 0; i <= stacks; i++) {
        float stack_angle = K_PI * 0.5f - static_cast<float>(i) * K_PI / static_cast<float>(stacks);
        float xy = cosf(stack_angle);
        float z = sinf(stack_angle);

        for (int j = 0; j <= sectors; j++) {
            float sector_angle = static_cast<float>(j) * 2.0f * K_PI / static_cast<float>(sectors);
            float x = xy * cosf(sector_angle);
            float y = xy * sinf(sector_angle);

            vertices[static_cast<size_t>(v)].position = vec3_create(x, y, z);
            vertices[static_cast<size_t>(v)].normal = vec3_create(x, y, z);
            v++;
        }
    }

    int idx = 0;
    for (int i = 0; i < stacks; i++) {
        int k1 = i * (sectors + 1);
        int k2 = k1 + sectors + 1;

        for (int j = 0; j < sectors; j++, k1++, k2++) {
            if (i != 0) {
                indices[static_cast<size_t>(idx++)] = static_cast<uint32_t>(k1);
                indices[static_cast<size_t>(idx++)] = static_cast<uint32_t>(k2);
                indices[static_cast<size_t>(idx++)] = static_cast<uint32_t>(k1 + 1);
            }
            if (i != (stacks - 1)) {
                indices[static_cast<size_t>(idx++)] = static_cast<uint32_t>(k1 + 1);
                indices[static_cast<size_t>(idx++)] = static_cast<uint32_t>(k2);
                indices[static_cast<size_t>(idx++)] = static_cast<uint32_t>(k2 + 1);
            }
        }
    }

    sphere_mesh_.index_count = static_cast<uint32_t>(idx);

    VkDeviceSize vertex_size = vertices.size() * sizeof(Vertex);
    create_buffer(vertex_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &sphere_mesh_.vertex);

    void* data;
    vkMapMemory(device_, sphere_mesh_.vertex.memory, 0, vertex_size, 0, &data);
    std::memcpy(data, vertices.data(), vertex_size);
    vkUnmapMemory(device_, sphere_mesh_.vertex.memory);

    VkDeviceSize index_size = sphere_mesh_.index_count * sizeof(uint32_t);
    create_buffer(index_size,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &sphere_mesh_.index);

    vkMapMemory(device_, sphere_mesh_.index.memory, 0, index_size, 0, &data);
    std::memcpy(data, indices.data(), index_size);
    vkUnmapMemory(device_, sphere_mesh_.index.memory);
}

void Renderer::create_quad_mesh() {
    Vertex vertices[4] = {
        { vec3_create(-0.5f, -0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f) },
        { vec3_create( 0.5f, -0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f) },
        { vec3_create( 0.5f,  0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f) },
        { vec3_create(-0.5f,  0.5f, 0.0f), vec3_create(0.0f, 0.0f, 1.0f) }
    };

    uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };

    VkDeviceSize vertex_size = sizeof(vertices);
    create_buffer(vertex_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &quad_mesh_.vertex);

    void* data;
    vkMapMemory(device_, quad_mesh_.vertex.memory, 0, vertex_size, 0, &data);
    std::memcpy(data, vertices, vertex_size);
    vkUnmapMemory(device_, quad_mesh_.vertex.memory);

    quad_mesh_.index_count = 6;
    VkDeviceSize index_size = sizeof(indices);
    create_buffer(index_size,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &quad_mesh_.index);

    vkMapMemory(device_, quad_mesh_.index.memory, 0, index_size, 0, &data);
    std::memcpy(data, indices, index_size);
    vkUnmapMemory(device_, quad_mesh_.index.memory);
}

void Renderer::create_box_mesh() {
    Vertex vertices[24] = {
        { vec3_create(-0.5f, -0.5f,  0.5f), vec3_create( 0.0f,  0.0f,  1.0f) },
        { vec3_create( 0.5f, -0.5f,  0.5f), vec3_create( 0.0f,  0.0f,  1.0f) },
        { vec3_create( 0.5f,  0.5f,  0.5f), vec3_create( 0.0f,  0.0f,  1.0f) },
        { vec3_create(-0.5f,  0.5f,  0.5f), vec3_create( 0.0f,  0.0f,  1.0f) },
        { vec3_create( 0.5f, -0.5f, -0.5f), vec3_create( 0.0f,  0.0f, -1.0f) },
        { vec3_create(-0.5f, -0.5f, -0.5f), vec3_create( 0.0f,  0.0f, -1.0f) },
        { vec3_create(-0.5f,  0.5f, -0.5f), vec3_create( 0.0f,  0.0f, -1.0f) },
        { vec3_create( 0.5f,  0.5f, -0.5f), vec3_create( 0.0f,  0.0f, -1.0f) },
        { vec3_create(-0.5f,  0.5f,  0.5f), vec3_create( 0.0f,  1.0f,  0.0f) },
        { vec3_create( 0.5f,  0.5f,  0.5f), vec3_create( 0.0f,  1.0f,  0.0f) },
        { vec3_create( 0.5f,  0.5f, -0.5f), vec3_create( 0.0f,  1.0f,  0.0f) },
        { vec3_create(-0.5f,  0.5f, -0.5f), vec3_create( 0.0f,  1.0f,  0.0f) },
        { vec3_create(-0.5f, -0.5f, -0.5f), vec3_create( 0.0f, -1.0f,  0.0f) },
        { vec3_create( 0.5f, -0.5f, -0.5f), vec3_create( 0.0f, -1.0f,  0.0f) },
        { vec3_create( 0.5f, -0.5f,  0.5f), vec3_create( 0.0f, -1.0f,  0.0f) },
        { vec3_create(-0.5f, -0.5f,  0.5f), vec3_create( 0.0f, -1.0f,  0.0f) },
        { vec3_create( 0.5f, -0.5f,  0.5f), vec3_create( 1.0f,  0.0f,  0.0f) },
        { vec3_create( 0.5f, -0.5f, -0.5f), vec3_create( 1.0f,  0.0f,  0.0f) },
        { vec3_create( 0.5f,  0.5f, -0.5f), vec3_create( 1.0f,  0.0f,  0.0f) },
        { vec3_create( 0.5f,  0.5f,  0.5f), vec3_create( 1.0f,  0.0f,  0.0f) },
        { vec3_create(-0.5f, -0.5f, -0.5f), vec3_create(-1.0f,  0.0f,  0.0f) },
        { vec3_create(-0.5f, -0.5f,  0.5f), vec3_create(-1.0f,  0.0f,  0.0f) },
        { vec3_create(-0.5f,  0.5f,  0.5f), vec3_create(-1.0f,  0.0f,  0.0f) },
        { vec3_create(-0.5f,  0.5f, -0.5f), vec3_create(-1.0f,  0.0f,  0.0f) }
    };

    uint32_t indices[36] = {
        0,  1,  2,  2,  3,  0,
        4,  5,  6,  6,  7,  4,
        8,  9, 10, 10, 11,  8,
       12, 13, 14, 14, 15, 12,
       16, 17, 18, 18, 19, 16,
       20, 21, 22, 22, 23, 20
    };

    VkDeviceSize vertex_size = sizeof(vertices);
    create_buffer(vertex_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &box_mesh_.vertex);

    void* data;
    vkMapMemory(device_, box_mesh_.vertex.memory, 0, vertex_size, 0, &data);
    std::memcpy(data, vertices, vertex_size);
    vkUnmapMemory(device_, box_mesh_.vertex.memory);

    box_mesh_.index_count = 36;
    VkDeviceSize index_size = sizeof(indices);
    create_buffer(index_size,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &box_mesh_.index);

    vkMapMemory(device_, box_mesh_.index.memory, 0, index_size, 0, &data);
    std::memcpy(data, indices, index_size);
    vkUnmapMemory(device_, box_mesh_.index.memory);
}

void Renderer::draw_voxels(const Voxel* voxels, const Bounds3D& bounds, float voxel_size) {
    if (shadow_pass_active_) {
        return;
    }
    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(VOXEL_TOTAL) * sizeof(uint32_t);

    void* data;
    vkMapMemory(device_, voxel_ssbo_[current_frame_].memory, 0, buffer_size, 0, &data);

    uint32_t* packed = static_cast<uint32_t*>(data);
    for (int32_t i = 0; i < VOXEL_TOTAL; i++) {
        packed[i] = static_cast<uint32_t>(voxels[i].r) |
                   (static_cast<uint32_t>(voxels[i].g) << 8) |
                   (static_cast<uint32_t>(voxels[i].b) << 16) |
                   (static_cast<uint32_t>(voxels[i].active) << 24);
    }

    vkUnmapMemory(device_, voxel_ssbo_[current_frame_].memory);

    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, voxel_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            voxel_pipeline_layout_, 0, 1, &voxel_descriptor_sets_[current_frame_], 0, nullptr);

    VoxelPushConstants vpc;
    vpc.view = view_matrix_;
    vpc.projection = projection_matrix_;
    vpc.bounds_min = vec3_create(bounds.min_x, bounds.min_y, bounds.min_z);
    vpc.voxel_size = voxel_size;
    vpc.bounds_max = vec3_create(bounds.max_x, bounds.max_y, bounds.max_z);
    vpc.pad1 = 0.0f;
    vpc.camera_pos = camera_position_;
    vpc.pad2 = 0.0f;
    vpc.grid_x = VOXEL_GRID_X;
    vpc.grid_y = VOXEL_GRID_Y;
    vpc.grid_z = VOXEL_GRID_Z;
    vpc.pad3 = 0.0f;

    vkCmdPushConstants(command_buffers_[current_frame_], voxel_pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(VoxelPushConstants), &vpc);

    vkCmdDraw(command_buffers_[current_frame_], 3, 1, 0, 0);

    vkCmdBindPipeline(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, ball_pipeline_);
    vkCmdBindDescriptorSets(command_buffers_[current_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &shadow_descriptor_sets_[current_frame_], 0, nullptr);
    VkBuffer sphere_buffers[] = { sphere_mesh_.vertex.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers_[current_frame_], 0, 1, sphere_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffers_[current_frame_], sphere_mesh_.index.buffer, 0, VK_INDEX_TYPE_UINT32);
}

bool Renderer::create_voxel_resources() {
    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(VOXEL_TOTAL) * sizeof(uint32_t);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        create_buffer(buffer_size,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &voxel_ssbo_[i]);
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &voxel_descriptor_layout_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &voxel_descriptor_pool_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = voxel_descriptor_layout_;
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = voxel_descriptor_pool_;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts;

    if (vkAllocateDescriptorSets(device_, &alloc_info, voxel_descriptor_sets_) != VK_SUCCESS) {
        return false;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = voxel_ssbo_[i].buffer;
        buffer_info.offset = 0;
        buffer_info.range = buffer_size;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = voxel_descriptor_sets_[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    return true;
}

bool Renderer::create_voxel_pipeline() {
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(VoxelPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &voxel_descriptor_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;

    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &voxel_pipeline_layout_) != VK_SUCCESS) {
        return false;
    }

    size_t vert_size = 0, frag_size = 0;
    char* vert_code = read_file("shaders/voxel.vert.spv", &vert_size);
    char* frag_code = read_file("shaders/voxel.frag.spv", &frag_size);
    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        return false;
    }

    VkShaderModuleCreateInfo vert_module_info{};
    vert_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_module_info.codeSize = vert_size;
    vert_module_info.pCode = reinterpret_cast<uint32_t*>(vert_code);

    VkShaderModuleCreateInfo frag_module_info{};
    frag_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_module_info.codeSize = frag_size;
    frag_module_info.pCode = reinterpret_cast<uint32_t*>(frag_code);

    VkShaderModule vert_module;
    VkShaderModule frag_module;
    vkCreateShaderModule(device_, &vert_module_info, nullptr, &vert_module);
    vkCreateShaderModule(device_, &frag_module_info, nullptr, &frag_module);

    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_module;
    shader_stages[0].pName = "main";

    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_module;
    shader_stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain_extent_;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_FALSE;
    depth_stencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = voxel_pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &voxel_pipeline_);

    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);
    free(vert_code);
    free(frag_code);

    return result == VK_SUCCESS;
}

bool Renderer::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Patch";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "PatchEngine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = extensions;

    return vkCreateInstance(&create_info, nullptr, &instance_) == VK_SUCCESS;
}

bool Renderer::select_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    physical_device_ = devices[0];
    return true;
}

bool Renderer::find_queue_families() {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, queue_families.data());

    graphics_family_ = UINT32_MAX;
    present_family_ = UINT32_MAX;

    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family_ = i;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support);
        if (present_support) {
            present_family_ = i;
        }

        if (graphics_family_ != UINT32_MAX && present_family_ != UINT32_MAX) {
            break;
        }
    }

    return graphics_family_ != UINT32_MAX && present_family_ != UINT32_MAX;
}

bool Renderer::create_logical_device() {
    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_infos[2]{};
    uint32_t queue_count = 1;

    queue_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_infos[0].queueFamilyIndex = graphics_family_;
    queue_infos[0].queueCount = 1;
    queue_infos[0].pQueuePriorities = &queue_priority;

    if (graphics_family_ != present_family_) {
        queue_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[1].queueFamilyIndex = present_family_;
        queue_infos[1].queueCount = 1;
        queue_infos[1].pQueuePriorities = &queue_priority;
        queue_count = 2;
    }

    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = queue_count;
    create_info.pQueueCreateInfos = queue_infos;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = device_extensions;

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
    return true;
}

bool Renderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

    VkSurfaceFormatKHR surface_format = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = f;
            break;
        }
    }

    swapchain_format_ = surface_format.format;
    swapchain_extent_ = capabilities.currentExtent;

    if (swapchain_extent_.width == UINT32_MAX) {
        swapchain_extent_.width = static_cast<uint32_t>(window_.width());
        swapchain_extent_.height = static_cast<uint32_t>(window_.height());
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = swapchain_extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queue_family_indices[] = { graphics_family_, present_family_ };
    if (graphics_family_ != present_family_) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create_info.clipped = VK_TRUE;

    VkResult result = vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkCreateSwapchainKHR failed: %d (extent: %ux%u)\n",
                result, swapchain_extent_.width, swapchain_extent_.height);
        return false;
    }

    uint32_t retrieved_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &retrieved_count, nullptr);
    swapchain_images_.resize(retrieved_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &retrieved_count, swapchain_images_.data());

    swapchain_image_views_.resize(retrieved_count);
    for (uint32_t i = 0; i < retrieved_count; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]);
    }

    return true;
}

bool Renderer::create_render_pass() {
    VkAttachmentDescription attachments[2]{};

    attachments[0].format = swapchain_format_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    return vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_) == VK_SUCCESS;
}

bool Renderer::create_depth_resources() {
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = swapchain_extent_.width;
    image_info.extent.height = swapchain_extent_.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = depth_format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    vkCreateImage(device_, &image_info, nullptr, &depth_image_);

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, depth_image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device_, &alloc_info, nullptr, &depth_image_memory_);
    vkBindImageMemory(device_, depth_image_, depth_image_memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = depth_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    vkCreateImageView(device_, &view_info, nullptr, &depth_image_view_);
    return true;
}

bool Renderer::create_shadow_resources() {
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &depth_attachment;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;

    if (vkCreateRenderPass(device_, &rp, nullptr, &shadow_render_pass_) != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler.compareEnable = VK_TRUE;
    sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sampler.minLod = 0.0f;
    sampler.maxLod = 0.0f;
    sampler.maxAnisotropy = 1.0f;

    if (vkCreateSampler(device_, &sampler, nullptr, &shadow_sampler_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 2;
    layout_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &shadow_descriptor_layout_) != VK_SUCCESS) {
        return false;
    }

    VkDeviceSize ubo_size = sizeof(ShadowUniforms);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        create_buffer(ubo_size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &shadow_ubo_[i]);

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = SHADOW_MAP_SIZE;
        image_info.extent.height = SHADOW_MAP_SIZE;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = depth_format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device_, &image_info, nullptr, &shadow_image_[i]) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, shadow_image_[i], &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &alloc_info, nullptr, &shadow_image_memory_[i]) != VK_SUCCESS) {
            return false;
        }

        vkBindImageMemory(device_, shadow_image_[i], shadow_image_memory_[i], 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = shadow_image_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = depth_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &view_info, nullptr, &shadow_image_view_[i]) != VK_SUCCESS) {
            return false;
        }

        VkImageView attachments[] = { shadow_image_view_[i] };

        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = shadow_render_pass_;
        fb.attachmentCount = 1;
        fb.pAttachments = attachments;
        fb.width = SHADOW_MAP_SIZE;
        fb.height = SHADOW_MAP_SIZE;
        fb.layers = 1;

        if (vkCreateFramebuffer(device_, &fb, nullptr, &shadow_framebuffer_[i]) != VK_SUCCESS) {
            return false;
        }
    }

    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &shadow_descriptor_pool_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = shadow_descriptor_layout_;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = shadow_descriptor_pool_;
    alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc.pSetLayouts = layouts;

    if (vkAllocateDescriptorSets(device_, &alloc, shadow_descriptor_sets_) != VK_SUCCESS) {
        return false;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo img{};
        img.sampler = shadow_sampler_;
        img.imageView = shadow_image_view_[i];
        img.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo buf{};
        buf.buffer = shadow_ubo_[i].buffer;
        buf.offset = 0;
        buf.range = sizeof(ShadowUniforms);

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = shadow_descriptor_sets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &img;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = shadow_descriptor_sets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &buf;

        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    }

    return true;
}

bool Renderer::create_shadow_pipeline() {
    size_t vert_size = 0;
    char* vert_code = read_file("shaders/shadowmap.vert.spv", &vert_size);
    if (!vert_code) {
        free(vert_code);
        return false;
    }

    VkShaderModuleCreateInfo vert_module_info{};
    vert_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_module_info.codeSize = vert_size;
    vert_module_info.pCode = reinterpret_cast<uint32_t*>(vert_code);

    VkShaderModule vert_module;
    if (vkCreateShaderModule(device_, &vert_module_info, nullptr, &vert_module) != VK_SUCCESS) {
        free(vert_code);
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vert_module;
    stage.pName = "main";

    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr_descs[2]{};
    attr_descs[0].binding = 0;
    attr_descs[0].location = 0;
    attr_descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));

    attr_descs[1].binding = 0;
    attr_descs[1].location = 1;
    attr_descs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attr_descs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 2.0f;
    rasterizer.depthBiasSlopeFactor = 2.0f;
    rasterizer.depthBiasClamp = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 0;
    color_blending.pAttachments = nullptr;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 1;
    pipeline_info.pStages = &stage;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = shadow_render_pass_;
    pipeline_info.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &shadow_pipeline_);

    vkDestroyShaderModule(device_, vert_module, nullptr);
    free(vert_code);

    return result == VK_SUCCESS;
}

void Renderer::update_shadow_uniforms() {
    ShadowUniforms u{};

    Vec3 light_dir = vec3_normalize(vec3_create(-0.6f, 0.9f, 0.35f));
    Vec3 light_forward = vec3_scale(light_dir, -1.0f);
    float extent = ortho_base_width_ > 0.0f ? ortho_base_width_ * 1.4f : 24.0f;
    float near_plane = 0.1f;
    float far_plane = 140.0f;

    Vec3 eye = vec3_sub(camera_target_, vec3_scale(light_forward, 70.0f));
    Mat4 light_view = mat4_look_at(eye, camera_target_, vec3_create(0.0f, 1.0f, 0.0f));
    Mat4 light_proj = mat4_ortho(-extent, extent, -extent, extent, near_plane, far_plane);
    u.light_view_proj = mat4_multiply(light_proj, light_view);
    u.light_dir = { light_dir.x, light_dir.y, light_dir.z, 0.0f };

    void* mapped = nullptr;
    vkMapMemory(device_, shadow_ubo_[current_frame_].memory, 0, sizeof(ShadowUniforms), 0, &mapped);
    memcpy(mapped, &u, sizeof(ShadowUniforms));
    vkUnmapMemory(device_, shadow_ubo_[current_frame_].memory);
}

bool Renderer::create_pipelines() {
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &shadow_descriptor_layout_;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;

    if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        return false;
    }

    bool ok_ball = create_pipeline(
        "shaders/ball.vert.spv",
        "shaders/ball.frag.spv",
        false,
        true,
        VK_CULL_MODE_BACK_BIT,
        &ball_pipeline_);

    bool ok_shadow = create_shadow_pipeline();

    bool ok_ui = create_pipeline(
        "shaders/ui.vert.spv",
        "shaders/ui.frag.spv",
        true,
        false,
        VK_CULL_MODE_NONE,
        &ui_pipeline_);

    return ok_ball && ok_shadow && ok_ui;
}

bool Renderer::create_pipeline(const char* vert_path, const char* frag_path,
                               bool enable_blend, bool depth_write,
                               VkCullModeFlags cull_mode, VkPipeline* out_pipeline) {
    size_t vert_size = 0, frag_size = 0;
    char* vert_code = read_file(vert_path, &vert_size);
    char* frag_code = read_file(frag_path, &frag_size);
    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        return false;
    }

    VkShaderModuleCreateInfo vert_module_info{};
    vert_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_module_info.codeSize = vert_size;
    vert_module_info.pCode = reinterpret_cast<uint32_t*>(vert_code);

    VkShaderModuleCreateInfo frag_module_info{};
    frag_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_module_info.codeSize = frag_size;
    frag_module_info.pCode = reinterpret_cast<uint32_t*>(frag_code);

    VkShaderModule vert_module;
    VkShaderModule frag_module;
    vkCreateShaderModule(device_, &vert_module_info, nullptr, &vert_module);
    vkCreateShaderModule(device_, &frag_module_info, nullptr, &frag_module);

    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_module;
    shader_stages[0].pName = "main";

    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_module;
    shader_stages[1].pName = "main";

    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr_descs[2]{};
    attr_descs[0].binding = 0;
    attr_descs[0].location = 0;
    attr_descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));

    attr_descs[1].binding = 0;
    attr_descs[1].location = 1;
    attr_descs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 2;
    vertex_input_info.pVertexAttributeDescriptions = attr_descs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain_extent_;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = cull_mode;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (enable_blend) {
        color_blend_attachment.blendEnable = VK_TRUE;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, out_pipeline);

    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);
    free(vert_code);
    free(frag_code);

    return result == VK_SUCCESS;
}

static void cmd_set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D extent) {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

bool Renderer::create_framebuffers() {
    framebuffers_.resize(swapchain_image_views_.size());

    for (size_t i = 0; i < swapchain_image_views_.size(); i++) {
        VkImageView attachments[] = { swapchain_image_views_[i], depth_image_view_ };

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = swapchain_extent_.width;
        framebuffer_info.height = swapchain_extent_.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool Renderer::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = graphics_family_;

    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    return vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_) == VK_SUCCESS;
}

bool Renderer::create_sync_objects() {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

void Renderer::destroy_swapchain_objects() {
    for (auto fb : framebuffers_) {
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();

    if (depth_image_view_) {
        vkDestroyImageView(device_, depth_image_view_, nullptr);
        depth_image_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_) {
        vkDestroyImage(device_, depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    if (depth_image_memory_) {
        vkFreeMemory(device_, depth_image_memory_, nullptr);
        depth_image_memory_ = VK_NULL_HANDLE;
    }

    if (ball_pipeline_) {
        vkDestroyPipeline(device_, ball_pipeline_, nullptr);
        ball_pipeline_ = VK_NULL_HANDLE;
    }
    if (shadow_pipeline_) {
        vkDestroyPipeline(device_, shadow_pipeline_, nullptr);
        shadow_pipeline_ = VK_NULL_HANDLE;
    }
    if (ui_pipeline_) {
        vkDestroyPipeline(device_, ui_pipeline_, nullptr);
        ui_pipeline_ = VK_NULL_HANDLE;
    }
    if (voxel_pipeline_) {
        vkDestroyPipeline(device_, voxel_pipeline_, nullptr);
        voxel_pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        destroy_buffer(&shadow_ubo_[i]);

        if (shadow_framebuffer_[i]) {
            vkDestroyFramebuffer(device_, shadow_framebuffer_[i], nullptr);
            shadow_framebuffer_[i] = VK_NULL_HANDLE;
        }
        if (shadow_image_view_[i]) {
            vkDestroyImageView(device_, shadow_image_view_[i], nullptr);
            shadow_image_view_[i] = VK_NULL_HANDLE;
        }
        if (shadow_image_[i]) {
            vkDestroyImage(device_, shadow_image_[i], nullptr);
            shadow_image_[i] = VK_NULL_HANDLE;
        }
        if (shadow_image_memory_[i]) {
            vkFreeMemory(device_, shadow_image_memory_[i], nullptr);
            shadow_image_memory_[i] = VK_NULL_HANDLE;
        }
    }

    if (shadow_sampler_) {
        vkDestroySampler(device_, shadow_sampler_, nullptr);
        shadow_sampler_ = VK_NULL_HANDLE;
    }
    if (shadow_descriptor_pool_) {
        vkDestroyDescriptorPool(device_, shadow_descriptor_pool_, nullptr);
        shadow_descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (shadow_descriptor_layout_) {
        vkDestroyDescriptorSetLayout(device_, shadow_descriptor_layout_, nullptr);
        shadow_descriptor_layout_ = VK_NULL_HANDLE;
    }
    if (voxel_pipeline_layout_) {
        vkDestroyPipelineLayout(device_, voxel_pipeline_layout_, nullptr);
        voxel_pipeline_layout_ = VK_NULL_HANDLE;
    }

    if (render_pass_) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    if (shadow_render_pass_) {
        vkDestroyRenderPass(device_, shadow_render_pass_, nullptr);
        shadow_render_pass_ = VK_NULL_HANDLE;
    }

    for (auto view : swapchain_image_views_) {
        if (view) vkDestroyImageView(device_, view, nullptr);
    }
    swapchain_image_views_.clear();
    swapchain_images_.clear();

    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool Renderer::recreate_swapchain() {
    if (device_ == VK_NULL_HANDLE) {
        return false;
    }
    vkDeviceWaitIdle(device_);
    destroy_swapchain_objects();

    if (!create_swapchain()) return false;
    if (!create_render_pass()) return false;
    if (!create_depth_resources()) return false;
    if (!create_shadow_resources()) return false;
    if (!create_pipelines()) return false;
    if (!create_voxel_pipeline()) return false;
    if (!create_framebuffers()) return false;
    return true;
}

}
