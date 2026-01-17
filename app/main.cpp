#include "engine/platform/window.h"
#include "engine/platform/platform.h"
#include "engine/render/renderer.h"
#include "engine/render/ui_renderer.h"
#include "engine/sim/scene.h"
#include "app/app_ui.h"
#include "app/app_debug.h"
#include "game/ball_pit.h"
#include "game/ball_pit_renderer.h"
#include "engine/core/rng.h"
#include "engine/core/profile.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

using namespace patch;

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
    bool f1_was_down = false;
    bool f2_was_down = false;
    bool f3_was_down = false;
    bool f4_was_down = false;
    bool f5_was_down = false;
    bool mouse_was_down = false;
    bool show_overlay = false;
    int32_t dbg_total_uploaded = 0;
    DebugSceneInfo dbg_info = {};
    DebugExportFeedback dbg_feedback = {};
    float fps_smooth = 0.0f;

    /* Free camera state */
    bool free_camera_active = false;
    bool free_camera_mouse_captured = false;
    Vec3 free_camera_pos = vec3_create(20.0f, 12.0f, 20.0f);
    float free_camera_yaw = -135.0f;
    float free_camera_pitch = -30.0f;
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
                free_camera_pos = renderer.get_camera_position();
                renderer.set_perspective(60.0f, 0.1f, 200.0f);
            }
            else
            {
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

        /* Mouse click detection */
        bool mouse_down = window.mouse().left_down;
        bool mouse_clicked = !mouse_down && mouse_was_down;
        mouse_was_down = mouse_down;

        /* Update feedback timer */
        if (dbg_feedback.timer > 0.0f)
            dbg_feedback.timer -= dt;

        bool do_export = f3_pressed;
        (void)do_export;

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

        case APP_ACTION_GRAPHICS:
            app_ui_show_screen(&ui, APP_SCREEN_GRAPHICS);
            break;

        case APP_ACTION_BACK:
            if (ui.current_screen == APP_SCREEN_GRAPHICS)
            {
                app_ui_show_screen(&ui, APP_SCREEN_SETTINGS);
            }
            else if (ui.current_screen == APP_SCREEN_SETTINGS)
            {
                AppScreen target = (ui.previous_screen == APP_SCREEN_PAUSE) ? APP_SCREEN_PAUSE : APP_SCREEN_MAIN_MENU;
                app_ui_show_screen(&ui, target);
            }
            else
            {
                app_ui_show_screen(&ui, ui.previous_screen != APP_SCREEN_NONE ? ui.previous_screen : APP_SCREEN_MAIN_MENU);
            }
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

        /* Free camera controls */
        if (free_camera_active)
        {
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

            if (free_camera_mouse_captured)
            {
                float dx = window.mouse().x - last_mouse_x;
                float dy = window.mouse().y - last_mouse_y;
                last_mouse_x = window.mouse().x;
                last_mouse_y = window.mouse().y;

                const float sensitivity = 0.2f;
                free_camera_yaw -= dx * sensitivity;
                free_camera_pitch -= dy * sensitivity;

                if (free_camera_pitch > 89.0f)
                    free_camera_pitch = 89.0f;
                if (free_camera_pitch < -89.0f)
                    free_camera_pitch = -89.0f;
            }

            float yaw_rad = free_camera_yaw * 3.14159f / 180.0f;
            float pitch_rad = free_camera_pitch * 3.14159f / 180.0f;

            Vec3 forward;
            forward.x = cosf(pitch_rad) * sinf(yaw_rad);
            forward.y = sinf(pitch_rad);
            forward.z = cosf(pitch_rad) * cosf(yaw_rad);

            Vec3 right = vec3_cross(forward, vec3_create(0.0f, 1.0f, 0.0f));
            right = vec3_normalize(right);

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
            float content_y = active_scene->bounds.min_y + 2.0f;
            Vec3 center = vec3_create(
                (active_scene->bounds.min_x + active_scene->bounds.max_x) * 0.5f,
                content_y,
                (active_scene->bounds.min_z + active_scene->bounds.max_z) * 0.5f);

            renderer.set_view_angle_at(45.0f, 40.0f, center, dt);
        }

        uint32_t image_index;
        renderer.begin_frame(&image_index);

        const AppSettings *settings = app_ui_get_settings(&ui);
        renderer.set_adaptive_quality(settings->adaptive_quality != 0);
        if (!settings->adaptive_quality)
        {
            renderer.set_shadow_quality(settings->shadow_quality);
            renderer.set_shadow_contact_hardening(settings->shadow_contact_hardening != 0);
            renderer.set_ao_quality(settings->ao_quality);
            renderer.set_lod_quality(settings->lod_quality);
            renderer.set_reflection_quality(settings->reflection_quality);
            renderer.set_rt_quality(settings->shadow_quality > 0 ? settings->shadow_quality : 1);
        }

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

            if (terrain)
            {
                bool need_depth_prime = (objects && objects->object_count > 0) || particles;
                renderer.prepare_gbuffer_compute(terrain, nullptr, need_depth_prime);
            }

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
                float interp_alpha = active_scene->sim_accumulator / SIM_TIMESTEP;
                if (interp_alpha < 0.0f) interp_alpha = 0.0f;
                if (interp_alpha > 1.0f) interp_alpha = 1.0f;
                renderer.set_interp_alpha(interp_alpha);
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

        /* Populate debug info */
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

        if (dbg_has_info && (do_export || btn_clicked))
        {
            const char *debug_filename = "debug_info.txt";
            const char *profile_filename = "profile_results.csv";
            dbg_feedback.success = export_all_debug(debug_filename, profile_filename, &dbg_info, fps_smooth, &renderer);
            snprintf(dbg_feedback.filename, sizeof(dbg_feedback.filename), "%s + %s", debug_filename, profile_filename);
            dbg_feedback.timer = 3.0f;
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
    return patch_main(argc, argv);
}
