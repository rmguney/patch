#include "app_debug.h"
#include "engine/core/profile.h"
#include <cstdio>
#include <ctime>
#include <cmath>

#ifdef PATCH_PROFILE
void export_profile_csv(const char *filename, const patch::Renderer *renderer)
{
    FILE *f = fopen(filename, "w");
    if (!f)
        return;

    time_t now = time(nullptr);
    fprintf(f, "# Profile export: %s", ctime(&now));
    if (renderer)
    {
        fprintf(f, "# GPU Device: %s\n", renderer->get_gpu_name());
        patch::Renderer::GPUTimings gpu;
        if (renderer->get_gpu_timings(&gpu))
        {
            fprintf(f, "# GPU Timings: shadow=%.3fms, main=%.3fms, total=%.3fms\n",
                    gpu.shadow_pass_ms, gpu.main_pass_ms, gpu.total_gpu_ms);
        }
    }
    fprintf(f, "# FPS: %.1f (avg), Frame: %.2fms (avg), %.2fms (max)\n",
            profile_get_avg_fps(),
            profile_get_avg_ms(PROFILE_FRAME_TOTAL),
            profile_get_max_ms(PROFILE_FRAME_TOTAL));
    fprintf(f, "# Budget: %.1f%% used, %d overruns, %.2fms worst\n",
            profile_budget_used_pct(),
            profile_budget_overruns(),
            profile_budget_worst_ms());
    fprintf(f, "category,avg_ms,max_ms,min_ms,p50_ms,p95_ms,samples\n");

    const char *names[] = {
        "frame_total", "sim_tick", "sim_physics", "sim_collision",
        "sim_voxel_update", "sim_connectivity", "sim_particles",
        "voxel_raycast", "voxel_edit", "voxel_occupancy", "voxel_upload",
        "render_total", "render_shadow", "render_main", "render_voxel", "render_ui",
        "volume_init", "prop_spawn"};

    for (int i = 0; i < PROFILE_COUNT && i < 18; i++)
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

    fclose(f);
}
#endif

bool export_debug_info(const char *filename, const DebugSceneInfo *info, float fps)
{
    FILE *f = fopen(filename, "w");
    if (!f)
        return false;

    fprintf(f, "=== PATCH DEBUG EXPORT ===\n\n");
    fprintf(f, "FPS: %.1f\n\n", fps);

    fprintf(f, "--- Content ---\n");
    fprintf(f, "Objects: %d\n", info->object_count);
    fprintf(f, "Chunks: %d (active: %d)\n", info->total_chunks, info->active_chunks);
    fprintf(f, "Solid voxels: %d\n", info->solid_voxels);
    fprintf(f, "Dirty queue: %d\n", info->dirty_queue_count);
    fprintf(f, "Total uploaded: %d\n", info->total_uploaded);
    fprintf(f, "Dirty overflow: %s\n\n", info->dirty_overflow ? "YES" : "no");

    fprintf(f, "--- Renderer ---\n");
    fprintf(f, "G-buffer init: %s\n", info->gbuffer_init ? "yes" : "NO");
    fprintf(f, "Voxel resources init: %s\n", info->voxel_res_init ? "yes" : "NO");
    fprintf(f, "Vobj resources init: %s\n", info->vobj_res_init ? "yes" : "NO");
    fprintf(f, "Terrain debug mode: %d\n\n", info->terrain_debug_mode);

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

    /* AABB intersection test */
    float ray_dir_x = -info->camera_pos[0];
    float ray_dir_y = -info->camera_pos[1] + 4.0f;
    float ray_dir_z = -info->camera_pos[2];
    float len = sqrtf(ray_dir_x * ray_dir_x + ray_dir_y * ray_dir_y + ray_dir_z * ray_dir_z);
    if (len > 0.001f)
    {
        ray_dir_x /= len;
        ray_dir_y /= len;
        ray_dir_z /= len;
    }

    fprintf(f, "Ray dir (to center): %.3f, %.3f, %.3f\n", ray_dir_x, ray_dir_y, ray_dir_z);

    /* Simple AABB test */
    float inv_x = 1.0f / (fabsf(ray_dir_x) > 0.0001f ? ray_dir_x : 0.0001f);
    float inv_y = 1.0f / (fabsf(ray_dir_y) > 0.0001f ? ray_dir_y : 0.0001f);
    float inv_z = 1.0f / (fabsf(ray_dir_z) > 0.0001f ? ray_dir_z : 0.0001f);
    float t0x = (info->bounds_min[0] - info->camera_pos[0]) * inv_x;
    float t1x = (info->bounds_max[0] - info->camera_pos[0]) * inv_x;
    float t0y = (info->bounds_min[1] - info->camera_pos[1]) * inv_y;
    float t1y = (info->bounds_max[1] - info->camera_pos[1]) * inv_y;
    float t0z = (info->bounds_min[2] - info->camera_pos[2]) * inv_z;
    float t1z = (info->bounds_max[2] - info->camera_pos[2]) * inv_z;
    float tmin_x = fminf(t0x, t1x), tmax_x = fmaxf(t0x, t1x);
    float tmin_y = fminf(t0y, t1y), tmax_y = fmaxf(t0y, t1y);
    float tmin_z = fminf(t0z, t1z), tmax_z = fmaxf(t0z, t1z);
    float tenter = fmaxf(fmaxf(tmin_x, tmin_y), tmin_z);
    float texit = fminf(fminf(tmax_x, tmax_y), tmax_z);
    bool hits_aabb = tenter <= texit && texit > 0.0f;
    fprintf(f, "AABB intersection: %s (enter=%.2f, exit=%.2f)\n", hits_aabb ? "HIT" : "MISS", tenter, texit);

    fclose(f);
    return true;
}

bool export_all_debug(const char *debug_filename, const char *profile_filename,
                      const DebugSceneInfo *info, float fps, const patch::Renderer *renderer)
{
    if (!export_debug_info(debug_filename, info, fps))
        return false;

#ifdef PATCH_PROFILE
    export_profile_csv(profile_filename, renderer);
#else
    (void)profile_filename;
    (void)renderer;
#endif

    return true;
}

bool draw_overlay(patch::Renderer &renderer, float fps, const BallPitStats *stats,
                  int32_t window_width, int32_t window_height,
                  const DebugSceneInfo *dbg,
                  float mouse_x, float mouse_y, bool mouse_clicked,
                  const DebugExportFeedback *feedback)
{
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

#ifdef PATCH_PROFILE
    float frame_ms = profile_get_last_ms(PROFILE_FRAME_TOTAL);
    float display_fps = frame_ms > 0.001f ? 1000.0f / frame_ms : fps;
    float display_ms = frame_ms > 0.001f ? frame_ms : (fps > 0.001f ? 1000.0f / fps : 0.0f);
#else
    float display_fps = fps;
    float display_ms = fps > 0.001f ? 1000.0f / fps : 0.0f;
#endif

    Vec3 fps_color = display_fps >= 55.0f ? vec3_create(0.4f, 0.9f, 0.4f) : display_fps >= 30.0f ? vec3_create(1.0f, 0.8f, 0.2f)
                                                                                                 : vec3_create(1.0f, 0.3f, 0.3f);
    snprintf(line, sizeof(line), "FPS %.0f (%.1fms)", display_fps, display_ms);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, fps_color, 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Raymarched Deferred");
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.82f, 0.9f, 1.0f), 1.0f, line);

    if (stats)
    {
        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "Spawns %d  Ticks %d", stats->spawn_count, stats->tick_count);
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.82f, 0.9f, 1.0f), 1.0f, line);
    }

    y_px += unit * 14.0f;
    snprintf(line, sizeof(line), "--- Debug (F2 toggle, F3 export, F4/F5 mode) ---");
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.9f, 0.6f), 1.0f, line);

#ifdef PATCH_PROFILE
    y_px += unit * 10.0f;
    float avg_fps = profile_get_avg_fps();
    float min_fps = profile_get_max_ms(PROFILE_FRAME_TOTAL) > 0.001f ? 1000.0f / profile_get_max_ms(PROFILE_FRAME_TOTAL) : 0.0f;
    snprintf(line, sizeof(line), "FPS: %.0f avg, %.0f min (worst frame)",
             avg_fps, min_fps);
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Frame: %.2fms avg, %.2fms max, %.2fms p99",
             profile_get_avg_ms(PROFILE_FRAME_TOTAL),
             profile_get_max_ms(PROFILE_FRAME_TOTAL),
             profile_get_p99_ms(PROFILE_FRAME_TOTAL));
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Budget: %.0f%% used, %d overruns",
             profile_budget_used_pct(), profile_budget_overruns());
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Sim: %.2fms (phys %.2fms, part %.2fms)",
             profile_get_avg_ms(PROFILE_SIM_TICK),
             profile_get_avg_ms(PROFILE_SIM_PHYSICS),
             profile_get_avg_ms(PROFILE_SIM_PARTICLES));
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.6f, 0.4f), 1.0f, line);
#endif

    y_px += unit * 10.0f;
    snprintf(line, sizeof(line), "Device: %s", renderer.get_gpu_name());
    renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.6f, 0.9f, 0.6f), 1.0f, line);

    patch::Renderer::GPUTimings gpu_timings;
    if (renderer.get_gpu_timings(&gpu_timings))
    {
        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "GPU: %.2fms (shadow %.2fms, main %.2fms)",
                 gpu_timings.total_gpu_ms, gpu_timings.shadow_pass_ms, gpu_timings.main_pass_ms);
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.8f, 0.7f, 1.0f), 1.0f, line);
    }

    if (dbg)
    {
        y_px += unit * 14.0f;
        snprintf(line, sizeof(line), "--- Scene Debug ---");
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 1.0f, 0.0f), 1.0f, line);

        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "OBJ: %d  CHUNKS: %d/%d  SOLID: %d",
                 dbg->object_count, dbg->active_chunks, dbg->total_chunks, dbg->solid_voxels);
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 1.0f, 0.0f), 1.0f, line);

        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "GBUF: %s  PIPE: %s  DESC: %s  VOXRES: %s  VOBJ: %s",
                 dbg->gbuffer_init ? "yes" : "NO",
                 dbg->gbuffer_pipeline_valid ? "yes" : "NO",
                 dbg->gbuffer_descriptors_valid ? "yes" : "NO",
                 dbg->voxel_res_init ? "yes" : "NO",
                 dbg->vobj_res_init ? "yes" : "NO");
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(1.0f, 0.5f, 0.0f), 1.0f, line);

        y_px += unit * 10.0f;
        snprintf(line, sizeof(line), "UPLOADED: %d  DIRTY_Q: %d  OVERFLOW: %s",
                 dbg->total_uploaded, dbg->dirty_queue_count,
                 dbg->dirty_overflow ? "YES" : "no");
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.5f, 1.0f, 0.5f), 1.0f, line);

        y_px += unit * 10.0f;
        const char *mode_names[] = {
            "Normal", "Normals", "Albedo", "Depth", "UVW", "Material", "Roughness",
            "Metallic", "ObjectID", "---", "WorldPos", "ShadowUVW", "Shadow",
            "AO", "Reflection", "GI"
        };
        const char *mode_name = (dbg->terrain_debug_mode >= 0 && dbg->terrain_debug_mode < 16)
            ? mode_names[dbg->terrain_debug_mode] : "?";
        snprintf(line, sizeof(line), "CAM: %.1f, %.1f, %.1f  MODE: %d (%s)  DRAWS: %d",
                 dbg->camera_pos[0], dbg->camera_pos[1], dbg->camera_pos[2],
                 dbg->terrain_debug_mode, mode_name, dbg->terrain_draw_count);
        renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.5f, 1.0f, 1.0f), 1.0f, line);

        y_px += unit * 12.0f;
        const float btn_w = unit * 60.0f;
        const float btn_h = unit * 12.0f;
        bool hovered = mouse_x >= x_px && mouse_x <= x_px + btn_w &&
                       mouse_y >= y_px && mouse_y <= y_px + btn_h;
        Vec3 btn_color = hovered ? vec3_create(0.4f, 0.6f, 0.9f) : vec3_create(0.2f, 0.4f, 0.7f);
        renderer.draw_ui_quad_px(x_px, y_px, btn_w, btn_h, btn_color, 0.9f);
        renderer.draw_ui_text_px(x_px + unit * 4.0f, y_px + unit * 2.0f, text_h_px,
                                 vec3_create(1.0f, 1.0f, 1.0f), 1.0f, "[EXPORT ALL] F3");

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

    renderer.end_ui();
    return false;
}
