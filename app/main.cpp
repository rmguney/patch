#include "engine/platform/window.h"
#include "engine/platform/platform.h"
#include "engine/render/renderer.h"
#include "engine/render/ui_renderer.h"
#include "engine/sim/scene.h"
#include "app/app_ui.h"
#include "game/ball_pit.h"
#include "game/ball_pit_renderer.h"
#include "engine/core/rng.h"
#include "engine/core/profile.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static BallPitParams params_from_settings(const AppSettings *s)
{
    BallPitParams p;
    p.initial_spawns = s->initial_spawns;
    p.spawn_interval = s->spawn_interval_ms / 1000.0f;
    p.spawn_batch = s->spawn_batch;
    p.max_spawns = s->max_spawns;
    return p;
}

#ifdef PATCH_PROFILE
static void export_profile_csv(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f)
        return;

    time_t now = time(nullptr);
    fprintf(f, "# Profile export: %s", ctime(&now));
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

using namespace patch;

/* DEBUG: Scene debug info structure for unified display and export */
struct DebugSceneInfo
{
    /* Content */
    int32_t object_count;
    int32_t total_chunks;
    int32_t solid_voxels;
    int32_t active_chunks;  /* Chunks with has_any==1 */
    int32_t dirty_queue_count;
    int32_t total_uploaded;
    bool dirty_overflow;

    /* Renderer state */
    bool gbuffer_init;
    bool gbuffer_pipeline_valid;
    bool gbuffer_descriptors_valid;
    bool voxel_res_init;
    bool vobj_res_init;
    int terrain_debug_mode;
    int terrain_draw_count;

    /* Bounds */
    float bounds_min[3];
    float bounds_max[3];

    /* Camera */
    float camera_pos[3];

    /* Volume info */
    int32_t chunks_x, chunks_y, chunks_z;
    float voxel_size;
};

/* DEBUG: Export feedback state */
struct DebugExportFeedback
{
    char filename[128];
    float timer;          /* Countdown to hide feedback */
    bool success;
};

/* DEBUG: Export debug info to file. Returns true on success. */
static bool export_debug_info(const char *filename, const DebugSceneInfo *info, float fps)
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
    float ray_dir_y = -info->camera_pos[1] + 4.0f;  /* Looking at y=4 center */
    float ray_dir_z = -info->camera_pos[2];
    float len = sqrtf(ray_dir_x * ray_dir_x + ray_dir_y * ray_dir_y + ray_dir_z * ray_dir_z);
    if (len > 0.001f) { ray_dir_x /= len; ray_dir_y /= len; ray_dir_z /= len; }

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

/* DEBUG: Draw debug overlay at bottom of screen. Returns true if export button clicked. */
static bool draw_debug_overlay(Renderer &renderer, const DebugSceneInfo *info, int32_t window_height,
                               float mouse_x, float mouse_y, bool mouse_clicked,
                               const DebugExportFeedback *feedback)
{
    if (!info)
        return false;

    char line[256];
    const float text_h = 14.0f;
    const float x = 20.0f;
    float y = (float)window_height - 40.0f;

    /* Line 1: Content info (yellow) */
    snprintf(line, sizeof(line), "[DEBUG] OBJ: %d  CHUNKS: %d/%d  SOLID: %d",
             info->object_count, info->active_chunks, info->total_chunks, info->solid_voxels);
    renderer.draw_ui_text_px(x, y, text_h, vec3_create(1.0f, 1.0f, 0.0f), 1.0f, line);

    /* Line 2: Renderer state (orange) */
    y -= 20.0f;
    snprintf(line, sizeof(line), "[DEBUG] GBUF: %s  PIPE: %s  DESC: %s  VOXRES: %s",
             info->gbuffer_init ? "yes" : "NO",
             info->gbuffer_pipeline_valid ? "yes" : "NO",
             info->gbuffer_descriptors_valid ? "yes" : "NO",
             info->voxel_res_init ? "yes" : "NO");
    renderer.draw_ui_text_px(x, y, text_h, vec3_create(1.0f, 0.5f, 0.0f), 1.0f, line);

    /* Line 3: Upload/dirty info (green) */
    y -= 20.0f;
    snprintf(line, sizeof(line), "[DEBUG] UPLOADED: %d  DIRTY_Q: %d  OVERFLOW: %s",
             info->total_uploaded, info->dirty_queue_count,
             info->dirty_overflow ? "YES" : "no");
    renderer.draw_ui_text_px(x, y, text_h, vec3_create(0.5f, 1.0f, 0.5f), 1.0f, line);

    /* Line 4: Camera info (cyan) */
    y -= 20.0f;
    snprintf(line, sizeof(line), "[DEBUG] CAM: %.1f, %.1f, %.1f  F6: %d  DRAWS: %d",
             info->camera_pos[0], info->camera_pos[1], info->camera_pos[2],
             info->terrain_debug_mode, info->terrain_draw_count);
    renderer.draw_ui_text_px(x, y, text_h, vec3_create(0.5f, 1.0f, 1.0f), 1.0f, line);

    /* Export button */
    y -= 24.0f;
    const float btn_w = 120.0f;
    const float btn_h = 20.0f;
    /* Both button and mouse use screen coords (y=0 at top) */
    bool hovered = mouse_x >= x && mouse_x <= x + btn_w &&
                   mouse_y >= y && mouse_y <= y + btn_h;
    Vec3 btn_color = hovered ? vec3_create(0.4f, 0.6f, 0.9f) : vec3_create(0.2f, 0.4f, 0.7f);
    renderer.draw_ui_quad_px(x, y, btn_w, btn_h, btn_color, 0.9f);
    renderer.draw_ui_text_px(x + 8.0f, y + 3.0f, text_h, vec3_create(1.0f, 1.0f, 1.0f), 1.0f, "[EXPORT] F5");

    /* Feedback message */
    if (feedback && feedback->timer > 0.0f)
    {
        Vec3 fb_color = feedback->success ? vec3_create(0.3f, 1.0f, 0.3f) : vec3_create(1.0f, 0.3f, 0.3f);
        snprintf(line, sizeof(line), "%s: %s",
                 feedback->success ? "Saved" : "Failed",
                 feedback->filename);
        renderer.draw_ui_text_px(x + btn_w + 10.0f, y + 3.0f, text_h, fb_color, 1.0f, line);
    }

    return hovered && mouse_clicked;
}

static void draw_frame_overlay(Renderer &renderer, float fps, const BallPitStats *stats,
                               int32_t window_width, int32_t window_height, bool show_profiling)
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

    if (show_profiling)
    {
        y_px += unit * 14.0f;
        snprintf(line, sizeof(line), "--- Profiler (F3 toggle, F4 export) ---");
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

        Renderer::GPUTimings gpu_timings;
        if (renderer.get_gpu_timings(&gpu_timings))
        {
            y_px += unit * 10.0f;
            snprintf(line, sizeof(line), "GPU: %.2fms (shadow %.2fms, main %.2fms)",
                     gpu_timings.total_gpu_ms, gpu_timings.shadow_pass_ms, gpu_timings.main_pass_ms);
            renderer.draw_ui_text_px(x_px, y_px, text_h_px, vec3_create(0.8f, 0.7f, 1.0f), 1.0f, line);
        }
    }

    renderer.end_ui();
}

enum class AppState
{
    Menu,
    Playing,
    Paused
};

enum class ActiveScene
{
    None,
    BallPit
};

int patch_main(int argc, char *argv[])
{
    int test_scene = -1;
    int test_frames = 0;
    const char *profile_csv = "profile_results.csv";

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
        {
            test_scene = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--test-frames") == 0 && i + 1 < argc)
        {
            test_frames = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--profile-csv") == 0 && i + 1 < argc)
        {
            profile_csv = argv[++i];
        }
    }

    if (test_scene >= SCENE_TYPE_COUNT)
    {
        fprintf(stderr, "Invalid scene ID: %d (max: %d)\n", test_scene, SCENE_TYPE_COUNT - 1);
        return 3;
    }

    printf("Patch\n\n");

    uint64_t rng_seed_value = 12345;
    const char *seed_env = getenv("PATCH_RNG_SEED");
    if (seed_env)
    {
        rng_seed_value = static_cast<uint64_t>(strtoull(seed_env, nullptr, 10));
    }

    Window window(1280, 720, "Patch");
    Renderer renderer(window);
    if (!renderer.init())
    {
        fprintf(stderr, "Renderer init failed: %s\n", renderer.get_init_error());
        return -1;
    }

    {
        MaterialEntry materials[MATERIAL_MAX_COUNT];
        for (int32_t i = 0; i < g_material_count; i++)
        {
            const MaterialDescriptor *mat = material_get(static_cast<uint8_t>(i));
            materials[i].r = static_cast<float>(mat->r) / 255.0f;
            materials[i].g = static_cast<float>(mat->g) / 255.0f;
            materials[i].b = static_cast<float>(mat->b) / 255.0f;
            materials[i].emissive = mat->emissive;
            materials[i].roughness = mat->roughness;
            materials[i].metallic = 0.0f;
            materials[i].flags = static_cast<float>(mat->flags);
            materials[i].pad = 0.0f;
        }
        renderer.set_material_palette_full(materials, g_material_count);
    }

    bool window_shown = false;

    AppUI ui;
    app_ui_init(&ui);

    AppState app_state = AppState::Menu;
    ActiveScene current_scene = ActiveScene::None;
    Scene *active_scene = nullptr;
    int frames_remaining = test_frames;

    if (test_scene >= 0)
    {
        const SceneDescriptor *desc = nullptr;
        switch (test_scene)
        {
        case SCENE_TYPE_BALL_PIT:
            desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
            active_scene = ball_pit_scene_create(desc->bounds, desc->voxel_size, nullptr);
            current_scene = ActiveScene::BallPit;
            break;
        }
        if (active_scene)
        {
            rng_seed(&active_scene->rng, rng_seed_value++);
            scene_init(active_scene);
            app_state = AppState::Playing;
            app_ui_hide(&ui);

            /* Initialize renderer resources for the scene */
            if (current_scene == ActiveScene::BallPit)
            {
                VoxelVolume *terrain = ball_pit_get_terrain(active_scene);
                if (terrain)
                {
                    renderer.init_volume_for_raymarching(terrain);
                }
                renderer.init_voxel_object_resources(VOBJ_MAX_OBJECTS);
            }

            printf("Test mode: scene %d (%s), frames %d\n", test_scene, desc->name, test_frames);
        }
    }

    platform_time_init();
    PlatformTime last_time = platform_time_now();

    renderer.set_orthographic(16.0f, 16.0f, 200.0f);
    renderer.set_view_angle(45.0f, 26.0f);

    bool escape_was_down = false;
    bool f3_was_down = false;
    bool f4_was_down = false;
    bool f5_was_down = false;  /* DEBUG: F5 export key */
    bool f6_was_down = false;  /* DEBUG: F6 terrain debug mode */
    bool mouse_was_down = false;  /* DEBUG: For button click detection */
    bool show_profiling = false;
    int32_t dbg_total_uploaded = 0;
    DebugSceneInfo dbg_info = {};  /* DEBUG: Unified debug info */
    DebugExportFeedback dbg_feedback = {};  /* DEBUG: Export feedback */
    float fps_smooth = 0.0f;

    while (!window.should_close())
    {
        PROFILE_BEGIN(PROFILE_FRAME_TOTAL);

        PlatformTime current_time = platform_time_now();
        float raw_dt = platform_time_delta_seconds(last_time, current_time);
        last_time = current_time;

        /* Clamp dt for physics stability only */
        float dt = raw_dt;
        if (dt > 0.1f)
            dt = 0.1f;

        window.poll_events();

        if (window.consume_resize() && window.width() > 0 && window.height() > 0)
        {
            renderer.on_resize();
        }

        bool escape_down = window.keys().escape;
        bool escape_pressed = escape_down && !escape_was_down;
        escape_was_down = escape_down;

        bool f3_down = window.keys().f3;
        bool f3_pressed = f3_down && !f3_was_down;
        f3_was_down = f3_down;

        bool f4_down = window.keys().f4;
        bool f4_pressed = f4_down && !f4_was_down;
        f4_was_down = f4_down;

        /* DEBUG: F5 to export debug info */
        bool f5_down = window.keys().f5;
        bool f5_pressed = f5_down && !f5_was_down;
        f5_was_down = f5_down;

        /* DEBUG: F6 to cycle terrain debug mode (0=normal, 1=AABB, 2=solid magenta) */
        bool f6_down = window.keys().f6;
        bool f6_pressed = f6_down && !f6_was_down;
        f6_was_down = f6_down;
        if (f6_pressed)
        {
            int mode = renderer.DEBUG_get_terrain_debug_mode();
            renderer.DEBUG_set_terrain_debug_mode((mode + 1) % 3);
        }

        if (escape_pressed)
        {
            if (app_state == AppState::Playing)
            {
                app_state = AppState::Paused;
                app_ui_show_screen(&ui, APP_SCREEN_PAUSE);
            }
            else if (app_state == AppState::Paused)
            {
                app_state = AppState::Playing;
                app_ui_hide(&ui);
            }
            else
            {
                window.request_close();
            }
        }

        if (f3_pressed)
        {
            show_profiling = !show_profiling;
#ifdef PATCH_PROFILE
            if (show_profiling)
            {
                profile_reset_all();
            }
#endif
        }

#ifdef PATCH_PROFILE
        if (f4_pressed && show_profiling)
        {
            export_profile_csv("profile_results.csv");
        }
#endif

        /* DEBUG: Mouse click detection (released this frame) */
        bool mouse_down = window.mouse().left_down;
        bool mouse_clicked = !mouse_down && mouse_was_down;
        mouse_was_down = mouse_down;

        /* DEBUG: Update feedback timer */
        if (dbg_feedback.timer > 0.0f)
            dbg_feedback.timer -= dt;

        /* DEBUG: F5 to export debug info (handled later with button click) */
        bool do_export = f5_pressed;
        (void)do_export;  /* Will be combined with button click below */

        app_ui_update(&ui, dt, window.mouse().x, window.mouse().y,
                      window.mouse().left_down, window.width(), window.height());

        AppAction action = app_ui_get_action(&ui);
        switch (action)
        {
        case APP_ACTION_START_BALL_PIT:
        {
            if (active_scene)
            {
                scene_destroy(active_scene);
            }
            const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_BALL_PIT);
            const AppSettings *settings = app_ui_get_settings(&ui);
            float voxel_size = settings->voxel_size_mm / 1000.0f;
            BallPitParams bp = params_from_settings(settings);
            active_scene = ball_pit_scene_create(desc->bounds, voxel_size, &bp);
            rng_seed(&active_scene->rng, rng_seed_value++);
            scene_init(active_scene);
            current_scene = ActiveScene::BallPit;
            app_state = AppState::Playing;
            app_ui_hide(&ui);
            renderer.set_orthographic(16.0f, 16.0f, 200.0f);

            /* Initialize renderer resources for the new scene */
            VoxelVolume *terrain = ball_pit_get_terrain(active_scene);
            if (terrain)
            {
                renderer.init_volume_for_raymarching(terrain);
            }
            renderer.init_voxel_object_resources(VOBJ_MAX_OBJECTS);

            printf("Started: %s\n", scene_get_name(active_scene));
            break;
        }

        case APP_ACTION_RUN_STRESS_TEST:
        {
            printf("Running stress test in background...\n");
            const char *cmd = "start \"Stress Test\" cmd /c \"test_render_perf.exe patch_samples.exe & pause\"";
            system(cmd);
            break;
        }

        case APP_ACTION_RESUME:
            app_state = AppState::Playing;
            app_ui_hide(&ui);
            break;

        case APP_ACTION_SCENE_SELECT:
            app_ui_show_screen(&ui, APP_SCREEN_SCENE_SELECT);
            break;

        case APP_ACTION_SETTINGS:
            app_ui_show_screen(&ui, APP_SCREEN_SETTINGS);
            break;

        case APP_ACTION_BACK:
            app_ui_show_screen(&ui, ui.previous_screen != APP_SCREEN_NONE ? ui.previous_screen : APP_SCREEN_MAIN_MENU);
            break;

        case APP_ACTION_MAIN_MENU:
            if (active_scene)
            {
                scene_destroy(active_scene);
                active_scene = nullptr;
                current_scene = ActiveScene::None;
            }
            app_state = AppState::Menu;
            app_ui_show_screen(&ui, APP_SCREEN_MAIN_MENU);
            renderer.set_orthographic(16.0f, 16.0f, 200.0f);
            break;

        case APP_ACTION_QUIT:
            window.request_close();
            break;

        default:
            break;
        }

        if (app_state == AppState::Playing && active_scene && !app_ui_is_blocking(&ui))
        {
            Vec3 ray_origin, ray_dir;
            renderer.screen_to_ray(window.mouse().x, window.mouse().y, &ray_origin, &ray_dir);

            if (current_scene == ActiveScene::BallPit)
            {
                ball_pit_set_ray(active_scene, ray_origin, ray_dir);
                scene_handle_input(active_scene, window.mouse().x, window.mouse().y,
                                   window.mouse().left_down, window.mouse().right_down);
            }

            scene_update(active_scene, dt);
        }

        if (active_scene && current_scene == ActiveScene::BallPit)
        {
            Vec3 center = vec3_create(
                (active_scene->bounds.min_x + active_scene->bounds.max_x) * 0.5f,
                (active_scene->bounds.min_y + active_scene->bounds.max_y) * 0.5f,
                (active_scene->bounds.min_z + active_scene->bounds.max_z) * 0.5f);

            renderer.set_view_angle_at(45.0f, 40.0f, center, dt);
        }

        uint32_t image_index;
        renderer.begin_frame(&image_index);

        PROFILE_BEGIN(PROFILE_RENDER_TOTAL);

        PROFILE_BEGIN(PROFILE_RENDER_MAIN);

        const BallPitStats *overlay_stats = nullptr;

        if (active_scene && current_scene == ActiveScene::BallPit)
        {
            BallPitData *data = (BallPitData *)active_scene->user_data;
            if (data)
            {
                overlay_stats = &data->stats;
            }

            /* Get scene systems for rendering */
            VoxelVolume *terrain = ball_pit_get_terrain(active_scene);
            VoxelObjectWorld *objects = ball_pit_get_objects(active_scene);
            ParticleSystem *particles = ball_pit_get_particles(active_scene);

            /* Prepare and upload dirty terrain chunks to GPU */
            if (terrain)
            {
                volume_begin_frame(terrain);
                int32_t dirty_indices[VOLUME_MAX_DIRTY_PER_FRAME];
                int32_t uploaded = renderer.upload_dirty_chunks(terrain, dirty_indices, VOLUME_MAX_DIRTY_PER_FRAME);
                if (uploaded > 0)
                {
                    dbg_total_uploaded += uploaded;
                    volume_mark_chunks_uploaded(terrain, dirty_indices, uploaded);
                }
            }

            /* G-buffer deferred rendering pipeline */
            renderer.begin_gbuffer_pass();

            if (terrain)
            {
                renderer.render_gbuffer_terrain(terrain);
            }

            if (objects)
            {
                renderer.render_voxel_objects_raymarched(objects);
            }

            if (particles)
            {
                renderer.render_particles_raymarched(particles);
            }

            renderer.end_gbuffer_pass();
            renderer.render_deferred_lighting(image_index);
            /* Note: render_deferred_lighting already begins the main render pass */
        }
        else
        {
            renderer.begin_main_pass(image_index);
        }
        PROFILE_END(PROFILE_RENDER_MAIN);

        PROFILE_BEGIN(PROFILE_RENDER_UI);
        draw_frame_overlay(renderer, fps_smooth, overlay_stats, window.width(), window.height(), show_profiling);

        /* DEBUG: Populate unified debug info and draw overlay */
        if (active_scene && current_scene == ActiveScene::BallPit)
        {
            VoxelObjectWorld *dbg_objects = ball_pit_get_objects(active_scene);
            VoxelVolume *dbg_terrain = ball_pit_get_terrain(active_scene);

            /* Populate debug info struct */
            dbg_info.object_count = dbg_objects ? dbg_objects->object_count : -1;
            dbg_info.total_chunks = dbg_terrain ? dbg_terrain->total_chunks : 0;
            dbg_info.active_chunks = dbg_terrain ? dbg_terrain->active_chunks : 0;
            dbg_info.solid_voxels = dbg_terrain ? dbg_terrain->total_solid_voxels : 0;
            dbg_info.dirty_queue_count = dbg_terrain ? dbg_terrain->dirty_count : 0;
            dbg_info.total_uploaded = dbg_total_uploaded;
            dbg_info.dirty_overflow = dbg_terrain && dbg_terrain->dirty_ring_overflow;
            dbg_info.gbuffer_init = renderer.DEBUG_is_gbuffer_initialized();
            dbg_info.gbuffer_pipeline_valid = renderer.DEBUG_is_gbuffer_pipeline_valid();
            dbg_info.gbuffer_descriptors_valid = renderer.DEBUG_is_gbuffer_descriptors_valid();
            dbg_info.voxel_res_init = renderer.DEBUG_is_voxel_resources_initialized();
            dbg_info.vobj_res_init = renderer.DEBUG_is_vobj_resources_initialized();
            dbg_info.terrain_debug_mode = renderer.DEBUG_get_terrain_debug_mode();
            dbg_info.terrain_draw_count = renderer.DEBUG_get_terrain_draw_count();

            if (dbg_terrain)
            {
                dbg_info.bounds_min[0] = dbg_terrain->bounds.min_x;
                dbg_info.bounds_min[1] = dbg_terrain->bounds.min_y;
                dbg_info.bounds_min[2] = dbg_terrain->bounds.min_z;
                dbg_info.bounds_max[0] = dbg_terrain->bounds.max_x;
                dbg_info.bounds_max[1] = dbg_terrain->bounds.max_y;
                dbg_info.bounds_max[2] = dbg_terrain->bounds.max_z;
                dbg_info.chunks_x = dbg_terrain->chunks_x;
                dbg_info.chunks_y = dbg_terrain->chunks_y;
                dbg_info.chunks_z = dbg_terrain->chunks_z;
                dbg_info.voxel_size = dbg_terrain->voxel_size;
            }

            /* Camera position */
            Vec3 cam = renderer.get_camera_position();
            dbg_info.camera_pos[0] = cam.x;
            dbg_info.camera_pos[1] = cam.y;
            dbg_info.camera_pos[2] = cam.z;

            /* DEBUG: Draw overlay and check for button click */
            /* Note: mouse Y is in top-left coords (0 at top), overlay uses bottom-left (0 at bottom) */
            bool btn_clicked = draw_debug_overlay(renderer, &dbg_info, window.height(),
                                                  window.mouse().x, window.mouse().y,
                                                  mouse_clicked, &dbg_feedback);

            /* DEBUG: Export on F5 or button click */
            if (do_export || btn_clicked)
            {
                const char *filename = "debug_info.txt";
                dbg_feedback.success = export_debug_info(filename, &dbg_info, fps_smooth);
                snprintf(dbg_feedback.filename, sizeof(dbg_feedback.filename), "%s", filename);
                dbg_feedback.timer = 3.0f;  /* Show for 3 seconds */
            }
        }

        ui_render(&ui.ctx, app_ui_get_active_menu(&ui), renderer, window.width(), window.height());
        PROFILE_END(PROFILE_RENDER_UI);

        PROFILE_END(PROFILE_RENDER_TOTAL);

        renderer.end_frame(image_index);

        if (!window_shown && test_frames == 0)
        {
            window.show();
            window_shown = true;
        }

        if (test_frames > 0)
        {
            frames_remaining--;
            if (frames_remaining <= 0)
            {
                printf("Test mode: completed %d frames\n", test_frames);
                PROFILE_END(PROFILE_FRAME_TOTAL);
                PROFILE_FRAME_END();
#ifdef PATCH_PROFILE
                export_profile_csv(profile_csv);
                printf("Profile exported to %s\n", profile_csv);
#endif
                break;
            }
        }

        PROFILE_END(PROFILE_FRAME_TOTAL);
        PROFILE_FRAME_END();

        /* Update FPS from profiler (single source of truth) */
#ifdef PATCH_PROFILE
        float fps = profile_get_fps();
        fps_smooth = fps_smooth < 0.01f ? fps : fps_smooth * 0.9f + fps * 0.1f;
#endif
    }

    if (active_scene)
    {
        scene_destroy(active_scene);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    return patch_main(argc, argv);
}
