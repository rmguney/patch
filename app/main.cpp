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
#include <cmath>

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
static void export_profile_csv(const char *filename, const patch::Renderer *renderer)
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

using namespace patch;

/* DEBUG: Scene debug info structure for unified display and export */
struct DebugSceneInfo
{
    /* Content */
    int32_t object_count;
    int32_t total_chunks;
    int32_t solid_voxels;
    int32_t active_chunks; /* Chunks with has_any==1 */
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
    float timer; /* Countdown to hide feedback */
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
    float ray_dir_y = -info->camera_pos[1] + 4.0f; /* Looking at y=4 center */
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

static bool export_all_debug(const char *debug_filename, const char *profile_filename,
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

static bool draw_overlay(Renderer &renderer, float fps, const BallPitStats *stats,
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

    Renderer::GPUTimings gpu_timings;
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
        snprintf(line, sizeof(line), "CAM: %.1f, %.1f, %.1f  MODE: %d  DRAWS: %d",
                 dbg->camera_pos[0], dbg->camera_pos[1], dbg->camera_pos[2],
                 dbg->terrain_debug_mode, dbg->terrain_draw_count);
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
        /* Convert sRGB colors to linear space for correct lighting calculations.
         * Material RGB values are defined in sRGB (what color pickers use).
         * The shader performs lighting in linear space then applies gamma correction. */
        auto srgb_to_linear = [](float srgb) -> float {
            if (srgb <= 0.04045f)
                return srgb / 12.92f;
            return powf((srgb + 0.055f) / 1.055f, 2.4f);
        };

        MaterialEntry materials[MATERIAL_MAX_COUNT];
        for (int32_t i = 0; i < g_material_count; i++)
        {
            const MaterialDescriptor *mat = material_get(static_cast<uint8_t>(i));
            float r_srgb = static_cast<float>(mat->r) / 255.0f;
            float g_srgb = static_cast<float>(mat->g) / 255.0f;
            float b_srgb = static_cast<float>(mat->b) / 255.0f;
            materials[i].r = srgb_to_linear(r_srgb);
            materials[i].g = srgb_to_linear(g_srgb);
            materials[i].b = srgb_to_linear(b_srgb);
            materials[i].emissive = mat->emissive;
            materials[i].roughness = mat->roughness;
            materials[i].metallic = mat->metallic;
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
    bool f1_was_down = false;    /* F1: Camera mode switch */
    bool f2_was_down = false;    /* F2: Debug overlay toggle */
    bool f3_was_down = false;    /* F3: Export debug info */
    bool f4_was_down = false;    /* F4: Terrain debug mode next */
    bool f5_was_down = false;    /* F5: Terrain debug mode previous */
    bool mouse_was_down = false; /* For button click detection */
    bool show_overlay = false;
    int32_t dbg_total_uploaded = 0;
    DebugSceneInfo dbg_info = {};          /* DEBUG: Unified debug info */
    DebugExportFeedback dbg_feedback = {}; /* DEBUG: Export feedback */
    float fps_smooth = 0.0f;

    /* DEBUG: Free camera state */
    bool free_camera_active = false;
    bool free_camera_mouse_captured = false;
    Vec3 free_camera_pos = vec3_create(20.0f, 12.0f, 20.0f);
    float free_camera_yaw = -135.0f;  /* degrees, looking toward origin */
    float free_camera_pitch = -30.0f; /* degrees, isometric view */
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    while (!window.should_close())
    {
        PROFILE_BEGIN(PROFILE_FRAME_TOTAL);

        PlatformTime current_time = platform_time_now();
        float raw_dt = platform_time_delta_seconds(last_time, current_time);
        last_time = current_time;

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

        /* F1: Camera mode switch */
        bool f1_down = window.keys().f1;
        bool f1_pressed = f1_down && !f1_was_down;
        f1_was_down = f1_down;
        if (f1_pressed)
        {
            free_camera_active = !free_camera_active;
            if (free_camera_active)
            {
                /* Initialize free camera from current view */
                free_camera_pos = renderer.get_camera_position();
                renderer.set_perspective(60.0f, 0.1f, 200.0f);
            }
            else
            {
                /* Restore orthographic projection */
                renderer.set_orthographic(16.0f, 16.0f, 200.0f);
                window.set_mouse_capture(false);
                window.set_cursor_visible(true);
                free_camera_mouse_captured = false;
            }
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

        /* F2: Debug overlay toggle */
        bool f2_down = window.keys().f2;
        bool f2_pressed = f2_down && !f2_was_down;
        f2_was_down = f2_down;
        if (f2_pressed)
        {
            show_overlay = !show_overlay;
#ifdef PATCH_PROFILE
            if (show_overlay)
            {
                profile_reset_all();
            }
#endif
        }

        /* F3: Export debug info */
        bool f3_down = window.keys().f3;
        bool f3_pressed = f3_down && !f3_was_down;
        f3_was_down = f3_down;

        /* F4: Terrain debug mode next */
        bool f4_down = window.keys().f4;
        bool f4_pressed = f4_down && !f4_was_down;
        f4_was_down = f4_down;
        if (f4_pressed)
        {
            int mode = renderer.DEBUG_get_terrain_debug_mode();
            renderer.DEBUG_set_terrain_debug_mode((mode + 1) % 13);
        }

        /* F5: Terrain debug mode previous */
        bool f5_down = window.keys().f5;
        bool f5_pressed = f5_down && !f5_was_down;
        f5_was_down = f5_down;
        if (f5_pressed)
        {
            int mode = renderer.DEBUG_get_terrain_debug_mode();
            renderer.DEBUG_set_terrain_debug_mode((mode + 12) % 13);
        }

        /* Mouse click detection (released this frame) */
        bool mouse_down = window.mouse().left_down;
        bool mouse_clicked = !mouse_down && mouse_was_down;
        mouse_was_down = mouse_down;

        /* Update feedback timer */
        if (dbg_feedback.timer > 0.0f)
            dbg_feedback.timer -= dt;

        /* F3 to export debug info (handled later with button click) */
        bool do_export = f3_pressed;
        (void)do_export; /* Will be combined with button click below */

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

        /* DEBUG: Free camera controls */
        if (free_camera_active)
        {
            /* Right click to capture/release mouse for looking */
            if (window.mouse().right_down && !free_camera_mouse_captured)
            {
                free_camera_mouse_captured = true;
                window.set_mouse_capture(true);
                window.set_cursor_visible(false);
                last_mouse_x = window.mouse().x;
                last_mouse_y = window.mouse().y;
            }
            else if (!window.mouse().right_down && free_camera_mouse_captured)
            {
                free_camera_mouse_captured = false;
                window.set_mouse_capture(false);
                window.set_cursor_visible(true);
            }

            /* Mouse look when captured */
            if (free_camera_mouse_captured)
            {
                float dx = window.mouse().x - last_mouse_x;
                float dy = window.mouse().y - last_mouse_y;
                last_mouse_x = window.mouse().x;
                last_mouse_y = window.mouse().y;

                const float sensitivity = 0.2f;
                free_camera_yaw -= dx * sensitivity;
                free_camera_pitch -= dy * sensitivity;

                /* Clamp pitch to avoid gimbal lock */
                if (free_camera_pitch > 89.0f)
                    free_camera_pitch = 89.0f;
                if (free_camera_pitch < -89.0f)
                    free_camera_pitch = -89.0f;
            }

            /* Calculate camera forward/right vectors */
            float yaw_rad = free_camera_yaw * 3.14159f / 180.0f;
            float pitch_rad = free_camera_pitch * 3.14159f / 180.0f;

            Vec3 forward;
            forward.x = cosf(pitch_rad) * sinf(yaw_rad);
            forward.y = sinf(pitch_rad);
            forward.z = cosf(pitch_rad) * cosf(yaw_rad);

            Vec3 right = vec3_cross(forward, vec3_create(0.0f, 1.0f, 0.0f));
            right = vec3_normalize(right);

            /* WASD movement */
            const float move_speed = 20.0f * dt;
            if (window.keys().w)
                free_camera_pos = vec3_add(free_camera_pos, vec3_scale(forward, move_speed));
            if (window.keys().s)
                free_camera_pos = vec3_sub(free_camera_pos, vec3_scale(forward, move_speed));
            if (window.keys().d)
                free_camera_pos = vec3_add(free_camera_pos, vec3_scale(right, move_speed));
            if (window.keys().a)
                free_camera_pos = vec3_sub(free_camera_pos, vec3_scale(right, move_speed));
            if (window.keys().space)
                free_camera_pos.y += move_speed;
            if (window.keys().shift)
                free_camera_pos.y -= move_speed;

            Vec3 target = vec3_add(free_camera_pos, forward);
            renderer.set_look_at(free_camera_pos, target);
        }
        else if (active_scene && current_scene == ActiveScene::BallPit)
        {
            /* Target lower in Y to center on actual content (floor + objects) rather than geometric center */
            float content_y = active_scene->bounds.min_y + 2.0f;
            Vec3 center = vec3_create(
                (active_scene->bounds.min_x + active_scene->bounds.max_x) * 0.5f,
                content_y,
                (active_scene->bounds.min_z + active_scene->bounds.max_z) * 0.5f);

            renderer.set_view_angle_at(45.0f, 40.0f, center, dt);
        }

        uint32_t image_index;
        renderer.begin_frame(&image_index);

        /* Apply quality settings */
        const AppSettings *settings = app_ui_get_settings(&ui);
        renderer.set_adaptive_quality(settings->adaptive_quality != 0);
        if (!settings->adaptive_quality)
            renderer.set_rt_quality(settings->rt_quality);

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

            VoxelVolume *terrain = ball_pit_get_terrain(active_scene);
            VoxelObjectWorld *objects = ball_pit_get_objects(active_scene);
            ParticleSystem *particles = ball_pit_get_particles(active_scene);

            bool has_objects_or_particles = (objects && objects->object_count > 0) ||
                                            (particles && particles->count > 0);

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

                if (uploaded > 0 || has_objects_or_particles)
                {
                    renderer.update_shadow_volume(terrain, objects, particles);
                }
            }

            /* Dispatch compute terrain before render pass (if using compute path) */
            if (terrain)
            {
                renderer.prepare_gbuffer_compute(terrain, nullptr);
            }

            /* Begin render pass - uses load pass if compute was dispatched */
            renderer.begin_gbuffer_pass();

            /* Render terrain (fragment path only, no-op if compute was used) */
            if (terrain)
            {
                renderer.render_gbuffer_terrain(terrain);
            }

            /* Render objects via fragment path (hardware rasterization is faster than compute for objects) */
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
        }
        else
        {
            renderer.begin_main_pass(image_index);
        }
        PROFILE_END(PROFILE_RENDER_MAIN);

        PROFILE_BEGIN(PROFILE_RENDER_UI);

        /* DEBUG: Populate unified debug info */
        bool dbg_has_info = false;
        if (active_scene && current_scene == ActiveScene::BallPit)
        {
            VoxelObjectWorld *dbg_objects = ball_pit_get_objects(active_scene);
            VoxelVolume *dbg_terrain = ball_pit_get_terrain(active_scene);

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

            Vec3 cam = renderer.get_camera_position();
            dbg_info.camera_pos[0] = cam.x;
            dbg_info.camera_pos[1] = cam.y;
            dbg_info.camera_pos[2] = cam.z;

            dbg_has_info = true;
        }

        bool btn_clicked = false;
        if (show_overlay)
        {
            btn_clicked = draw_overlay(renderer, fps_smooth, overlay_stats,
                                       window.width(), window.height(),
                                       dbg_has_info ? &dbg_info : nullptr,
                                       window.mouse().x, window.mouse().y,
                                       mouse_clicked, &dbg_feedback);
        }

        /* Export on F3 or button click (exports all: debug + profile if available) */
        if (dbg_has_info && (do_export || btn_clicked))
        {
            const char *debug_filename = "debug_info.txt";
            const char *profile_filename = "profile_results.csv";
            dbg_feedback.success = export_all_debug(debug_filename, profile_filename, &dbg_info, fps_smooth, &renderer);
            snprintf(dbg_feedback.filename, sizeof(dbg_feedback.filename), "%s + %s", debug_filename, profile_filename);
            dbg_feedback.timer = 3.0f; /* Show for 3 seconds */
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
                export_profile_csv(profile_csv, &renderer);
                printf("Profile exported to %s\n", profile_csv);
#endif
                break;
            }
        }

        PROFILE_END(PROFILE_FRAME_TOTAL);
        PROFILE_FRAME_END();

        /* Update adaptive quality based on frame time */
        renderer.update_adaptive_quality(profile_get_avg_ms(PROFILE_FRAME_TOTAL));

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
    printf("DEBUG: patch_samples starting (console output enabled)\n");
    fflush(stdout);
    return patch_main(argc, argv);
}
