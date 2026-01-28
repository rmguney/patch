#include "app_debug.h"
#include "engine/core/profile.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>

void debug_info_clear(DebugInfo *info)
{
    memset(info, 0, sizeof(DebugInfo));
    info->scene_name = "Unknown";
    info->gpu_name = "Unknown";
}

void debug_info_populate_volume(DebugInfo *info, const VoxelVolume *terrain)
{
    if (!terrain)
        return;

    info->total_chunks = terrain->total_chunks;
    info->active_chunks = terrain->active_chunks;
    info->solid_voxels = terrain->total_solid_voxels;
    info->dirty_queue_count = terrain->dirty_count;
    info->dirty_overflow = terrain->dirty_ring_overflow;
    info->chunks_x = terrain->chunks_x;
    info->chunks_y = terrain->chunks_y;
    info->chunks_z = terrain->chunks_z;
    info->voxel_size = terrain->voxel_size;
    info->bounds_min[0] = terrain->bounds.min_x;
    info->bounds_min[1] = terrain->bounds.min_y;
    info->bounds_min[2] = terrain->bounds.min_z;
    info->bounds_max[0] = terrain->bounds.max_x;
    info->bounds_max[1] = terrain->bounds.max_y;
    info->bounds_max[2] = terrain->bounds.max_z;
}

void debug_info_populate_objects(DebugInfo *info, const VoxelObjectWorld *objects)
{
    if (!objects)
        return;

    info->object_count = objects->object_count;
}

void debug_info_populate_particles(DebugInfo *info, const ParticleSystem *particles)
{
    if (!particles)
        return;

    info->particle_count = particles->count;
}

void debug_info_populate_renderer(DebugInfo *info, const patch::Renderer *renderer)
{
    if (!renderer)
        return;

    info->gbuffer_init = renderer->DEBUG_is_gbuffer_initialized();
    info->gbuffer_pipeline_valid = renderer->DEBUG_is_gbuffer_pipeline_valid();
    info->gbuffer_descriptors_valid = renderer->DEBUG_is_gbuffer_descriptors_valid();
    info->voxel_res_init = renderer->DEBUG_is_voxel_resources_initialized();
    info->vobj_res_init = renderer->DEBUG_is_vobj_resources_initialized();
    info->terrain_debug_mode = renderer->DEBUG_get_terrain_debug_mode();
    info->terrain_draw_count = renderer->DEBUG_get_terrain_draw_count();

    Vec3 cam_pos = renderer->get_camera_position();
    Vec3 cam_fwd = renderer->get_camera_forward();
    info->camera_pos[0] = cam_pos.x;
    info->camera_pos[1] = cam_pos.y;
    info->camera_pos[2] = cam_pos.z;
    info->camera_fwd[0] = cam_fwd.x;
    info->camera_fwd[1] = cam_fwd.y;
    info->camera_fwd[2] = cam_fwd.z;

    info->gpu_name = renderer->get_gpu_name();
    info->cpu_fence_ms = renderer->get_last_wait_fence_ms();
    info->cpu_acquire_ms = renderer->get_last_acquire_ms();
    info->cpu_present_ms = renderer->get_last_present_ms();

    patch::Renderer::GPUTimings gpu;
    if (renderer->get_last_gpu_timings(&gpu))
    {
        info->gpu_shadow_ms = gpu.shadow_pass_ms;
        info->gpu_main_ms = gpu.main_pass_ms;
        info->gpu_total_ms = gpu.total_gpu_ms;
        info->gpu_gbuffer_ms = gpu.gbuffer_compute_ms;
        info->gpu_temporal_shadow_ms = gpu.temporal_shadow_ms;
        info->gpu_ao_ms = gpu.ao_compute_ms;
        info->gpu_temporal_ao_ms = gpu.temporal_ao_ms;
        info->gpu_taa_ms = gpu.taa_resolve_ms;
        info->gpu_denoise_ms = gpu.spatial_denoise_ms;
        info->gpu_timings_valid = true;
    }
}

void debug_info_populate_profiler(DebugInfo *info)
{
#ifdef PATCH_PROFILE
    float frame_ms = profile_get_last_ms(PROFILE_FRAME_TOTAL);
    info->frame_ms_current = frame_ms;
    info->fps_current = frame_ms > 0.001f ? 1000.0f / frame_ms : 0.0f;

    info->fps_avg = profile_get_avg_fps();
    info->frame_ms_avg = profile_get_avg_ms(PROFILE_FRAME_TOTAL);
    info->frame_ms_max = profile_get_max_ms(PROFILE_FRAME_TOTAL);
    info->frame_ms_p50 = profile_get_p50_ms(PROFILE_FRAME_TOTAL);
    info->frame_ms_p95 = profile_get_p95_ms(PROFILE_FRAME_TOTAL);
    info->frame_ms_p99 = profile_get_p99_ms(PROFILE_FRAME_TOTAL);
    info->budget_pct = profile_budget_used_pct();
    info->budget_overruns = profile_budget_overruns();
    info->budget_worst_ms = profile_budget_worst_ms();

    info->sim_tick_ms = profile_get_avg_ms(PROFILE_SIM_TICK);
    info->sim_physics_ms = profile_get_avg_ms(PROFILE_SIM_PHYSICS);
    info->sim_particles_ms = profile_get_avg_ms(PROFILE_SIM_PARTICLES);
    info->sim_collision_ms = profile_get_avg_ms(PROFILE_SIM_COLLISION);

    info->render_total_ms = profile_get_avg_ms(PROFILE_RENDER_TOTAL);
    info->render_shadow_ms = profile_get_avg_ms(PROFILE_RENDER_SHADOW);
    info->render_main_ms = profile_get_avg_ms(PROFILE_RENDER_MAIN);
    info->render_gbuffer_ms = profile_get_avg_ms(PROFILE_RENDER_GBUFFER);
    info->render_objects_ms = profile_get_avg_ms(PROFILE_RENDER_OBJECTS);
    info->render_lighting_ms = profile_get_avg_ms(PROFILE_RENDER_LIGHTING);
    info->render_ao_ms = profile_get_avg_ms(PROFILE_RENDER_AO);
    info->render_denoise_ms = profile_get_avg_ms(PROFILE_RENDER_DENOISE);
    info->render_ui_overlay_ms = profile_get_avg_ms(PROFILE_RENDER_UI_OVERLAY);

    /* Sub-profiling for render_main (spike investigation) */
    info->render_volume_begin_ms = profile_get_avg_ms(PROFILE_RENDER_VOLUME_BEGIN);
    info->render_chunk_upload_ms = profile_get_avg_ms(PROFILE_RENDER_CHUNK_UPLOAD);
    info->render_shadow_volume_ms = profile_get_avg_ms(PROFILE_RENDER_SHADOW_VOLUME);
    info->render_main_max_ms = profile_get_max_ms(PROFILE_RENDER_MAIN);

    /* Trend detection */
    info->frame_trend_ratio = profile_get_trend_ratio(PROFILE_FRAME_TOTAL);
#endif
}

bool export_debug_report(const char *filename, const DebugInfo *info)
{
    FILE *f = fopen(filename, "w");
    if (!f)
        return false;

    time_t now = time(nullptr);
    fprintf(f, "================================================================================\n");
    fprintf(f, "                         PATCH DEBUG REPORT\n");
    fprintf(f, "================================================================================\n");
    fprintf(f, "Generated: %s\n", ctime(&now));

    fprintf(f, "--- Scene ---\n");
    fprintf(f, "Name: %s\n\n", info->scene_name);

    fprintf(f, "--- Performance ---\n");
    fprintf(f, "FPS: %.0f current, %.0f avg\n", info->fps_current, info->fps_avg);
    fprintf(f, "Frame: %.2fms avg, %.2fms max, %.2fms p99\n",
            info->frame_ms_avg, info->frame_ms_max, info->frame_ms_p99);
    fprintf(f, "Budget: %.0f%% used, %d overruns, %.2fms worst\n\n",
            info->budget_pct, info->budget_overruns, info->budget_worst_ms);

    fprintf(f, "--- GPU ---\n");
    fprintf(f, "Device: %s\n", info->gpu_name);
    if (info->gpu_timings_valid)
    {
        fprintf(f, "GPU: %.2fms total (gbuf %.2f, shad %.2f, tshad %.2f, ao %.2f, tao %.2f, rend %.2f, taa %.2f, den %.2f)\n",
                info->gpu_total_ms, info->gpu_gbuffer_ms, info->gpu_shadow_ms,
                info->gpu_temporal_shadow_ms, info->gpu_ao_ms, info->gpu_temporal_ao_ms,
                info->gpu_main_ms, info->gpu_taa_ms, info->gpu_denoise_ms);
    }
    fprintf(f, "CPU waits: fence=%.3fms, acquire=%.3fms, present=%.3fms\n\n",
            info->cpu_fence_ms, info->cpu_acquire_ms, info->cpu_present_ms);

    fprintf(f, "--- Sim Timing ---\n");
    fprintf(f, "Tick: %.2fms (physics %.2fms, particles %.2fms, collision %.2fms)\n\n",
            info->sim_tick_ms, info->sim_physics_ms, info->sim_particles_ms, info->sim_collision_ms);

    fprintf(f, "--- Render Timing ---\n");
    fprintf(f, "Total: %.2fms\n", info->render_total_ms);
    fprintf(f, "G-buffer: %.2fms, Objects: %.2fms\n", info->render_gbuffer_ms, info->render_objects_ms);
    fprintf(f, "Shadow: %.2fms, Main: %.2fms (max: %.2fms)\n",
            info->render_shadow_ms, info->render_main_ms, info->render_main_max_ms);
    fprintf(f, "  Main breakdown: vol_begin=%.3fms, chunk_upload=%.3fms, shadow_vol=%.3fms\n",
            info->render_volume_begin_ms, info->render_chunk_upload_ms, info->render_shadow_volume_ms);
    fprintf(f, "Lighting: %.2fms, AO: %.2fms, Denoise: %.2fms\n",
            info->render_lighting_ms, info->render_ao_ms, info->render_denoise_ms);
    fprintf(f, "UI Overlay: %.2fms\n\n", info->render_ui_overlay_ms);

    fprintf(f, "--- Content ---\n");
    fprintf(f, "Objects: %d\n", info->object_count);
    fprintf(f, "Particles: %d\n", info->particle_count);
    fprintf(f, "Spawns: %d, Ticks: %d\n", info->spawn_count, info->tick_count);
    fprintf(f, "Chunks: %d (active: %d)\n", info->total_chunks, info->active_chunks);
    fprintf(f, "Solid voxels: %d\n", info->solid_voxels);
    fprintf(f, "Dirty queue: %d\n", info->dirty_queue_count);
    fprintf(f, "Total uploaded: %d\n", info->total_uploaded);
    fprintf(f, "Dirty overflow: %s\n\n", info->dirty_overflow ? "YES" : "no");

    fprintf(f, "--- Renderer State ---\n");
    fprintf(f, "G-buffer init: %s\n", info->gbuffer_init ? "yes" : "NO");
    fprintf(f, "G-buffer pipeline: %s\n", info->gbuffer_pipeline_valid ? "yes" : "NO");
    fprintf(f, "G-buffer descriptors: %s\n", info->gbuffer_descriptors_valid ? "yes" : "NO");
    fprintf(f, "Voxel resources init: %s\n", info->voxel_res_init ? "yes" : "NO");
    fprintf(f, "Vobj resources init: %s\n", info->vobj_res_init ? "yes" : "NO");
    fprintf(f, "Terrain debug mode: %d\n", info->terrain_debug_mode);
    fprintf(f, "Terrain draw count: %d\n\n", info->terrain_draw_count);

    fprintf(f, "--- Volume ---\n");
    fprintf(f, "Chunks: %d x %d x %d = %d\n", info->chunks_x, info->chunks_y, info->chunks_z,
            info->chunks_x * info->chunks_y * info->chunks_z);
    fprintf(f, "Voxel size: %.4f\n", info->voxel_size);
    fprintf(f, "Grid size: %d x %d x %d voxels\n",
            info->chunks_x * 32, info->chunks_y * 32, info->chunks_z * 32);

    fprintf(f, "\n--- Bounds ---\n");
    fprintf(f, "Min: %.2f, %.2f, %.2f\n", info->bounds_min[0], info->bounds_min[1], info->bounds_min[2]);
    fprintf(f, "Max: %.2f, %.2f, %.2f\n", info->bounds_max[0], info->bounds_max[1], info->bounds_max[2]);
    fprintf(f, "Size: %.2f x %.2f x %.2f\n",
            info->bounds_max[0] - info->bounds_min[0],
            info->bounds_max[1] - info->bounds_min[1],
            info->bounds_max[2] - info->bounds_min[2]);

    fprintf(f, "\n--- Camera ---\n");
    fprintf(f, "Position: %.2f, %.2f, %.2f\n", info->camera_pos[0], info->camera_pos[1], info->camera_pos[2]);
    fprintf(f, "Forward: %.3f, %.3f, %.3f\n", info->camera_fwd[0], info->camera_fwd[1], info->camera_fwd[2]);

#ifdef PATCH_PROFILE
    fprintf(f, "\n================================================================================\n");
    fprintf(f, "                         PROFILE DATA (CSV)\n");
    fprintf(f, "================================================================================\n\n");

    /* Header comments for test parser (GPU timings, budget info, trend) */
    fprintf(f, "# GPU Timings: shadow=%.3fms, main=%.3fms, total=%.3fms\n",
            info->gpu_shadow_ms, info->gpu_main_ms, info->gpu_total_ms);
    fprintf(f, "# GPU Passes: gbuffer=%.3fms, shadow=%.3fms, tshadow=%.3fms, ao=%.3fms, tao=%.3fms, render=%.3fms, taa=%.3fms, denoise=%.3fms\n",
            info->gpu_gbuffer_ms, info->gpu_shadow_ms, info->gpu_temporal_shadow_ms,
            info->gpu_ao_ms, info->gpu_temporal_ao_ms, info->gpu_main_ms,
            info->gpu_taa_ms, info->gpu_denoise_ms);
    fprintf(f, "# Budget: %.1f%% used, %d overruns, %.2fms worst\n",
            info->budget_pct, info->budget_overruns, info->budget_worst_ms);
    fprintf(f, "# Trend: %.2fx (last_third/first_third, >1.5 = degradation)\n",
            info->frame_trend_ratio);

    fprintf(f, "category,avg_ms,max_ms,min_ms,p50_ms,p95_ms,samples\n");

    const char *names[] = {
        "frame_total", "sim_tick", "sim_physics", "sim_collision",
        "sim_voxel_update", "sim_connectivity", "sim_particles",
        "voxel_raycast", "voxel_edit", "voxel_occupancy", "voxel_upload",
        "render_total", "render_shadow", "render_main", "render_ui_overlay", "render_ui",
        "volume_init", "prop_spawn",
        "render_gbuffer", "render_objects", "render_lighting", "render_ao", "render_denoise",
        "render_volume_begin", "render_chunk_upload", "render_shadow_volume",
        "shadow_terrain_pack", "shadow_object_stamp", "shadow_mip_regen", "shadow_upload"};

    const int name_count = (int)(sizeof(names) / sizeof(names[0]));
    const int count = (PROFILE_COUNT < name_count) ? PROFILE_COUNT : name_count;
    for (int i = 0; i < count; i++)
    {
        fprintf(f, "%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n",
                names[i],
                profile_get_avg_ms((ProfileCategory)i),
                profile_get_max_ms((ProfileCategory)i),
                profile_get_min_ms((ProfileCategory)i),
                profile_get_p50_ms((ProfileCategory)i),
                profile_get_p95_ms((ProfileCategory)i),
                profile_get_sample_count((ProfileCategory)i));
    }
#endif

    fclose(f);
    return true;
}

bool draw_debug_overlay(patch::Renderer &renderer,
                        int32_t window_width, int32_t window_height,
                        const DebugInfo *info,
                        float mouse_x, float mouse_y, bool mouse_clicked,
                        const DebugExportFeedback *feedback)
{
    if (!info)
        return false;

    renderer.begin_ui();
    const float w = (float)((window_width > 0) ? window_width : 1);
    const float h = (float)((window_height > 0) ? window_height : 1);
    const float min_dim = (w < h) ? w : h;

    const float text_h_px = min_dim * 0.022f;
    const float unit = text_h_px / 7.0f;
    const float margin_px = 20.0f;
    float x_px = margin_px;
    float y_px = margin_px;
    char line[128];

    float display_fps = info->fps_current > 0.001f ? info->fps_current : info->fps_avg;
    float display_ms = info->frame_ms_current > 0.001f ? info->frame_ms_current : info->frame_ms_avg;

    Vec3 fps_color = display_fps >= 55.0f ? vec3_create(0.4f, 0.9f, 0.4f) : display_fps >= 30.0f ? vec3_create(1.0f, 0.8f, 0.2f)
                                                                                                 : vec3_create(1.0f, 0.3f, 0.3f);
    snprintf(line, sizeof(line), "FPS %.0f (%.1fms)", display_fps, display_ms);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, fps_color, 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Scene: %s", info->scene_name);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.82f, 0.9f, 1.0f), 1.0f, line);

    if (info->spawn_count > 0 || info->tick_count > 0)
    {
        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "Spawns %d  Ticks %d  Particles %d",
                 info->spawn_count, info->tick_count, info->particle_count);
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.82f, 0.9f, 1.0f), 1.0f, line);
    }

    y_px += unit * 14.0f;
    snprintf(line, sizeof(line), "--- Debug (F2 toggle, F3 export, F4/F5 mode) ---");
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.9f, 0.6f), 1.0f, line);

#ifdef PATCH_PROFILE
    y_px += unit * 10.0f;
    float min_fps = info->frame_ms_max > 0.001f ? 1000.0f / info->frame_ms_max : 0.0f;
    snprintf(line, sizeof(line), "FPS: %.0f avg, %.0f min (worst frame)", info->fps_avg, min_fps);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Frame: %.2fms avg, %.2fms max, %.2fms p99",
             info->frame_ms_avg, info->frame_ms_max, info->frame_ms_p99);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Budget: %.0f%% used, %d overruns",
             info->budget_pct, info->budget_overruns);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Sim: %.2fms (phys %.2fms, part %.2fms)",
             info->sim_tick_ms, info->sim_physics_ms, info->sim_particles_ms);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);
#endif

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Device: %s", info->gpu_name);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.6f, 0.9f, 0.6f), 1.0f, line);

    if (info->gpu_timings_valid)
    {
        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "GPU: %.1fms  gbuf %.1f shad %.1f ts %.1f ao %.1f ta %.1f rend %.1f taa %.1f den %.1f",
                 info->gpu_total_ms, info->gpu_gbuffer_ms, info->gpu_shadow_ms,
                 info->gpu_temporal_shadow_ms, info->gpu_ao_ms, info->gpu_temporal_ao_ms,
                 info->gpu_main_ms, info->gpu_taa_ms, info->gpu_denoise_ms);
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.8f, 0.7f, 1.0f), 1.0f, line);
    }

    y_px += unit * 14.0f;
    snprintf(line, sizeof(line), "--- Scene Debug ---");
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 1.0f, 0.0f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "OBJ: %d  CHUNKS: %d/%d  SOLID: %d",
             info->object_count, info->active_chunks, info->total_chunks, info->solid_voxels);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 1.0f, 0.0f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "GBUF: %s  PIPE: %s  DESC: %s  VOXRES: %s  VOBJ: %s",
             info->gbuffer_init ? "yes" : "NO",
             info->gbuffer_pipeline_valid ? "yes" : "NO",
             info->gbuffer_descriptors_valid ? "yes" : "NO",
             info->voxel_res_init ? "yes" : "NO",
             info->vobj_res_init ? "yes" : "NO");
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.5f, 0.0f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "UPLOADED: %d  DIRTY_Q: %d  OVERFLOW: %s",
             info->total_uploaded, info->dirty_queue_count,
             info->dirty_overflow ? "YES" : "no");
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.5f, 1.0f, 0.5f), 1.0f, line);

    y_px += unit * 10.0f;
    const char *mode_names[] = {
        "Normal", "Normals", "Albedo", "Depth", "UVW", "Material", "Roughness",
        "Metallic", "ObjectID", "---", "WorldPos", "ShadowUVW", "Shadow", "AO"};
    const char *mode_name = (info->terrain_debug_mode >= 0 && info->terrain_debug_mode < 14)
                                ? mode_names[info->terrain_debug_mode]
                                : "?";
    snprintf(line, sizeof(line), "CAM: %.1f, %.1f, %.1f  MODE: %d (%s)  DRAWS: %d",
             info->camera_pos[0], info->camera_pos[1], info->camera_pos[2],
             info->terrain_debug_mode, mode_name, info->terrain_draw_count);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.5f, 1.0f, 1.0f), 1.0f, line);

    y_px += unit * 12.0f;
    const float btn_w = unit * 60.0f;
    const float btn_h = unit * 12.0f;
    bool hovered = mouse_x >= x_px && mouse_x <= x_px + btn_w &&
                   mouse_y >= y_px && mouse_y <= y_px + btn_h;
    Vec3 btn_color = hovered ? vec3_create(0.4f, 0.6f, 0.9f) : vec3_create(0.2f, 0.4f, 0.7f);
    renderer.draw_ui_quad_px(x_px, y_px, btn_w, btn_h, btn_color, 0.9f);
    renderer.draw_ui_text_px(x_px + unit * 4.0f, y_px + unit * 2.0f, text_h_px,
                             vec3_create(1.0f, 1.0f, 1.0f), 1.0f, "[EXPORT] F3");

    if (feedback && feedback->timer > 0.0f)
    {
        Vec3 fb_color = feedback->success ? vec3_create(0.3f, 1.0f, 0.3f) : vec3_create(1.0f, 0.3f, 0.3f);
        snprintf(line, sizeof(line), "%s: %s",
                 feedback->success ? "Saved" : "Failed",
                 feedback->filename);
        renderer.draw_ui_text_px(x_px + btn_w + unit * 4.0f, y_px + unit * 2.0f, text_h_px, fb_color, 1.0f, line);
    }

    renderer.end_ui();
    return hovered && mouse_clicked;
}
