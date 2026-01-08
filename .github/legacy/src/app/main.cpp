#include "../engine/window.h"
#include "../engine/renderer.h"
#include "../engine/ui_renderer.h"
#include "../core/scene.h"
#include "../core/ui.h"
#include "../scenes/ball_pit.h"
#include "../scenes/ball_pit_renderer.h"
#include "../scenes/melee.h"
#include "../scenes/melee_renderer.h"
#include "../scenes/shooter.h"
#include "../scenes/shooter_renderer.h"
#include "../game/player.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace patch;

static void set_cursor_visible(bool visible) {
    if (visible) {
        while (ShowCursor(TRUE) < 0) {}
    } else {
        while (ShowCursor(FALSE) >= 0) {}
    }
}

static void draw_frame_overlay(Renderer& renderer, float fps, float survival_time, bool show_survival,
                               int enemies, int props, int particles,
                               int32_t window_width, int32_t window_height) {
    float ms = fps > 0.001f ? 1000.0f / fps : 0.0f;
    renderer.begin_ui();
    float min_dim = static_cast<float>(window_width < window_height ? window_width : window_height);
    float pixel = (2.0f / min_dim) * 2.5f;
    float margin_px = 20.0f;
    float x = -1.0f + margin_px * (2.0f / static_cast<float>(window_width));
    float y_ndc = 1.0f - margin_px * (2.0f / static_cast<float>(window_height));
    float y = -y_ndc;
    char line[96];
    snprintf(line, sizeof(line), "FPS %.0f (%.1fms)", fps, ms);
    renderer.draw_ui_text(x, y, pixel, vec3_create(0.75f, 0.35f, 0.38f), 1.0f, line);
    y -= pixel * 8.0f;
    if (show_survival) {
        snprintf(line, sizeof(line), "Time %.1fs", survival_time);
        renderer.draw_ui_text(x, y, pixel, vec3_create(0.98f, 0.86f, 0.55f), 1.0f, line);
        y -= pixel * 8.0f;
    }
    snprintf(line, sizeof(line), "Enemies %d  Props %d", enemies, props);
    renderer.draw_ui_text(x, y, pixel, vec3_create(0.82f, 0.9f, 1.0f), 1.0f, line);
    y -= pixel * 8.0f;
    snprintf(line, sizeof(line), "Particles %d", particles);
    renderer.draw_ui_text(x, y, pixel, vec3_create(0.82f, 0.9f, 1.0f), 1.0f, line);
    renderer.end_ui();
}

enum class AppState {
    Menu,
    Playing,
    Paused
};

enum class SceneType {
    None,
    BallPit,
    Melee,
    Shooter
};

int patch_main() {
    printf("Patch\n\n");

    srand(static_cast<unsigned int>(time(nullptr)));

    Window window(1280, 720, "Patch");
    Renderer renderer(window);
    bool window_shown = false;

    Bounds3D bounds;
    bounds.min_x = -4.0f;
    bounds.max_x = 4.0f;
    bounds.min_y = -2.5f;
    bounds.max_y = 4.0f;
    bounds.min_z = -4.0f;
    bounds.max_z = 4.0f;

    UIState ui;
    ui_init(&ui);

    AppState app_state = AppState::Menu;
    SceneType current_type = SceneType::None;
    Scene* active_scene = nullptr;

    Player menu_player;
    player_init(&menu_player, vec3_create(0.0f, bounds.min_y, 0.0f));

    LARGE_INTEGER frequency, last_time, current_time;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&last_time);

    renderer.set_orthographic(16.0f, 16.0f, 200.0f);
    renderer.set_view_angle(45.0f, 26.0f);

    bool escape_was_down = false;
    float fps_smooth = 0.0f;

    while (!window.should_close()) {
        QueryPerformanceCounter(&current_time);
        float dt = static_cast<float>(current_time.QuadPart - last_time.QuadPart) / static_cast<float>(frequency.QuadPart);
        last_time = current_time;
        if (dt > 0.033f) dt = 0.033f;
        float fps = dt > 0.0001f ? 1.0f / dt : 0.0f;
        fps_smooth = fps_smooth == 0.0f ? fps : fps_smooth * 0.9f + fps * 0.1f;

        window.poll_events();

        if (window.consume_resize() && window.width() > 0 && window.height() > 0) {
            renderer.on_resize();
        }

        bool escape_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool escape_pressed = escape_down && !escape_was_down;
        escape_was_down = escape_down;

        if (escape_pressed) {
            if (app_state == AppState::Playing) {
                app_state = AppState::Paused;
                ui_show_screen(&ui, UI_SCREEN_PAUSE);
            } else if (app_state == AppState::Paused) {
                app_state = AppState::Playing;
                ui_hide(&ui);
            } else {
                window.request_close();
            }
        }

        ui_update(&ui, dt, window.mouse().x, window.mouse().y, window.mouse().left_down, window.width(), window.height());

        UIAction action = ui_get_pending_action(&ui);
        switch (action) {
            case UI_ACTION_START_BALL_PIT:
                if (active_scene) {
                    scene_destroy(active_scene);
                }
                active_scene = ball_pit_scene_create(bounds);
                scene_init(active_scene);
                current_type = SceneType::BallPit;
                app_state = AppState::Playing;
                ui_hide(&ui);
                renderer.set_orthographic(16.0f, 16.0f, 200.0f);
                printf("Started: %s\n", scene_get_name(active_scene));
                break;

            case UI_ACTION_START_MELEE:
                if (active_scene) {
                    scene_destroy(active_scene);
                }
                active_scene = melee_scene_create(bounds);
                scene_init(active_scene);
                current_type = SceneType::Melee;
                app_state = AppState::Playing;
                ui_hide(&ui);
                renderer.set_orthographic(16.0f, 16.0f, 200.0f);
                printf("Started: %s\n", scene_get_name(active_scene));
                break;

            case UI_ACTION_START_SHOOTER:
                if (active_scene) {
                    scene_destroy(active_scene);
                }
                active_scene = shooter_scene_create(bounds);
                scene_init(active_scene);
                current_type = SceneType::Shooter;
                app_state = AppState::Playing;
                ui_hide(&ui);
                renderer.set_orthographic(16.0f, 16.0f, 200.0f);
                printf("Started: %s\n", scene_get_name(active_scene));
                break;

            case UI_ACTION_RESUME:
                app_state = AppState::Playing;
                ui_hide(&ui);
                break;

            case UI_ACTION_SCENE_SELECT:
                ui_show_screen(&ui, UI_SCREEN_SCENE_SELECT);
                break;

            case UI_ACTION_SETTINGS:
                ui_show_screen(&ui, UI_SCREEN_SETTINGS);
                break;

            case UI_ACTION_BACK:
                ui_show_screen(&ui, ui.previous_screen != UI_SCREEN_NONE ? ui.previous_screen : UI_SCREEN_MAIN_MENU);
                break;

            case UI_ACTION_MAIN_MENU:
                if (active_scene) {
                    scene_destroy(active_scene);
                    active_scene = nullptr;
                    current_type = SceneType::None;
                }
                app_state = AppState::Menu;
                ui_show_screen(&ui, UI_SCREEN_MAIN_MENU);
                renderer.set_orthographic(16.0f, 16.0f, 200.0f);
                break;

            case UI_ACTION_QUIT:
                window.request_close();
                break;

            default:
                break;
        }

        set_cursor_visible(true);

        if (app_state == AppState::Playing && active_scene && !ui_is_blocking(&ui)) {
            Vec3 ray_origin, ray_dir;
            renderer.screen_to_ray(window.mouse().x, window.mouse().y, &ray_origin, &ray_dir);

            if (current_type == SceneType::BallPit) {
                ball_pit_set_ray(active_scene, ray_origin, ray_dir);

                Vec3 mouse_world;
                bool mouse_valid = renderer.screen_to_world_floor(window.mouse().x, window.mouse().y, bounds.min_y + 0.5f, &mouse_world);
                ball_pit_set_mouse_world(active_scene, mouse_world, mouse_valid);

                scene_handle_input(active_scene, window.mouse().x, window.mouse().y, window.mouse().left_down, window.mouse().right_down);
            } else if (current_type == SceneType::Melee) {
                MeleeData* melee_data = melee_get_data(active_scene);
                
                if (melee_data && melee_data->player.is_dead && window.keys().r) {
                    scene_destroy(active_scene);
                    active_scene = melee_scene_create(bounds);
                    scene_init(active_scene);
                    melee_data = melee_get_data(active_scene);
                }
                
                melee_set_input(active_scene, 
                    window.keys().w, window.keys().a, window.keys().s, window.keys().d,
                    window.mouse().left_down, window.mouse().right_down);

                if (melee_data) {
                    melee_data->max_dead_bodies = ui.dead_body_limit;
                }

                scene_handle_input(active_scene, window.mouse().x, window.mouse().y, window.mouse().left_down, window.mouse().right_down);
            } else if (current_type == SceneType::Shooter) {
                ShooterData* shooter_data = shooter_get_data(active_scene);

                if (shooter_data && shooter_data->player.is_dead && window.keys().r) {
                    scene_destroy(active_scene);
                    active_scene = shooter_scene_create(bounds);
                    scene_init(active_scene);
                    shooter_data = shooter_get_data(active_scene);
                }

                shooter_set_input(active_scene,
                    window.keys().w, window.keys().a, window.keys().s, window.keys().d,
                    window.mouse().left_down, window.mouse().right_down);

                shooter_set_aim_ray(active_scene, ray_origin, ray_dir);

                if (shooter_data) {
                    shooter_data->max_dead_bodies = ui.dead_body_limit;
                }

                scene_handle_input(active_scene, window.mouse().x, window.mouse().y, window.mouse().left_down, window.mouse().right_down);
            }

            scene_update(active_scene, dt);
        }

        if (active_scene && current_type == SceneType::Melee) {
            MeleeData* melee_data = melee_get_data(active_scene);
            if (melee_data) {
                renderer.set_view_angle_at(45.0f, 26.0f * 1.5f, melee_data->player.position, dt);
            }
        } else if (active_scene && current_type == SceneType::Shooter) {
            ShooterData* shooter_data = shooter_get_data(active_scene);
            if (shooter_data) {
                renderer.set_view_angle_at(45.0f, 26.0f * 1.5f, shooter_data->player.position, dt);
            }
        }

        uint32_t image_index;
        renderer.begin_frame(&image_index);

        renderer.begin_shadow_pass();
        if (active_scene) {
            if (current_type == SceneType::BallPit) {
                ball_pit_render(active_scene, renderer);
            } else if (current_type == SceneType::Melee) {
                melee_render(active_scene, renderer);
            } else if (current_type == SceneType::Shooter) {
                shooter_render(active_scene, renderer);
            }
        } else {
            renderer.draw_pit(bounds);

            Vec3 base = menu_player.position;
            base.y = bounds.min_y;
            Vec3 player_color = vec3_create(0.20f, 0.60f, 0.85f);
            HumanoidPose pose = player_get_pose(&menu_player);
            renderer.draw_humanoid_voxels(base, &menu_player.model, &pose, player_color);
        }
        renderer.end_shadow_pass();

        renderer.begin_main_pass(image_index);

        int overlay_enemies = 0;
        int overlay_props = 0;
        int overlay_particles = 0;
        float overlay_survival = 0.0f;
        bool overlay_show_survival = false;

        if (active_scene) {
            if (current_type == SceneType::BallPit) {
                ball_pit_render(active_scene, renderer);
                BallPitData* data = (BallPitData*)active_scene->user_data;
                if (data) {
                    overlay_props = data->vobj_world ? data->vobj_world->object_count : 0;
                    overlay_particles = data->particles ? data->particles->count : 0;
                }
            } else if (current_type == SceneType::Melee) {
                MeleeData* melee_data = melee_get_data(active_scene);
                if (melee_data) {
                    overlay_enemies = melee_data->enemy_count;
                    overlay_props = melee_data->vobj_world ? melee_data->vobj_world->object_count : 0;
                    overlay_particles = melee_data->particles ? melee_data->particles->count : 0;
                    overlay_survival = melee_data->survival_time;
                    overlay_show_survival = true;
                }
                melee_render(active_scene, renderer);
            } else if (current_type == SceneType::Shooter) {
                ShooterData* shooter_data = shooter_get_data(active_scene);
                if (shooter_data) {
                    overlay_enemies = shooter_data->enemy_count;
                    overlay_props = shooter_data->vobj_world ? shooter_data->vobj_world->object_count : 0;
                    overlay_particles = shooter_data->particles ? shooter_data->particles->count : 0;
                    overlay_survival = shooter_data->survival_time;
                    overlay_show_survival = true;
                }
                shooter_render(active_scene, renderer);
            }
        } else {
            renderer.draw_pit(bounds);

            Vec3 base = menu_player.position;
            base.y = bounds.min_y;
            Vec3 player_color = vec3_create(0.20f, 0.60f, 0.85f);
            HumanoidPose pose = player_get_pose(&menu_player);
            renderer.draw_humanoid_voxels(base, &menu_player.model, &pose, player_color);
        }

        draw_frame_overlay(renderer, fps_smooth, overlay_survival, overlay_show_survival,
                           overlay_enemies, overlay_props, overlay_particles,
                           window.width(), window.height());

        ui_render(&ui, renderer, window.width(), window.height());

        renderer.end_frame(image_index);

        if (!window_shown) {
            window.show();
            window_shown = true;
        }
    }

    if (active_scene) {
        scene_destroy(active_scene);
    }

    return 0;
}

int main() {
    return patch_main();
}
