#include "engine/platform/window.h"
#include "engine/platform/platform.h"
#include "engine/render/renderer.h"
#include "engine/render/ui_renderer.h"
#include "engine/sim/scene.h"
#include "app/app_ui.h"
#include "app/app_debug.h"
#include "game/world_scene.h"
#include "game/destruction_script.h"
#include "engine/core/rng.h"
#include "engine/core/profile.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static constexpr uint64_t DEFAULT_RNG_SEED = 12345;
static constexpr float DEFAULT_FOV = 60.0f;
static constexpr float DEFAULT_NEAR = 0.1f;
static constexpr float DEFAULT_FAR = 200.0f;
static constexpr float MAX_FRAME_DT = 0.1f;
static constexpr float FREE_CAM_SENSITIVITY = 0.2f;
static constexpr float FREE_CAM_PITCH_LIMIT = 89.0f;
static constexpr float FREE_CAM_MOVE_SPEED = 20.0f;
/* Start pose: 80 units out from 2 above the scene's floor center at yaw 45 and 35.26 degrees
 * elevation (world scene: 46.2, 48.2, 46.2), facing back toward it with a 30 degree downward pitch */
static constexpr float START_CAM_DISTANCE = 80.0f;
static constexpr float START_CAM_ORBIT_YAW = 45.0f;
static constexpr float START_CAM_ELEVATION = 35.26f;
static constexpr float START_CAM_YAW = -135.0f;
static constexpr float START_CAM_PITCH = -30.0f;
/* Right button released within this distance of its press point is a click (throw), not a look drag */
static constexpr float RIGHT_CLICK_MAX_MOVE_PX = 4.0f;
/* --test-resize (test runs): the window cycles through these client sizes, one every
 * TEST_RESIZE_INTERVAL frames, and the frames in between must render at exactly that size:
 * degenerate and odd sizes, 1080p, and 1440p (beyond a 1080p monitor) */
static constexpr int32_t TEST_RESIZE_SIZES[][2] = {{1, 1}, {1, 97}, {97, 1}, {1023, 577}, {3, 2}, {2560, 1440}, {640, 360}, {1920, 1080}};
static constexpr int TEST_RESIZE_INTERVAL = 4;
/* Exit codes test_launch tells apart (3 is abort()'s) */
static constexpr int EXIT_INVALID_SCENE = 2;
static constexpr int EXIT_INVALID_ARGUMENT = 5; /* a command-line value did not parse */
static constexpr int EXIT_TEST_FAILED = 4; /* a test run failed a check or ended before its last frame */

using namespace patch;

/* Free camera pose; yaw / pitch in degrees */
struct FreeCamera
{
    Vec3 pos;
    float yaw;
    float pitch;
};

static FreeCamera free_camera_start_pose(const Bounds3D &bounds)
{
    Vec3 center = vec3_create(
        (bounds.min_x + bounds.max_x) * 0.5f,
        bounds.min_y + 2.0f,
        (bounds.min_z + bounds.max_z) * 0.5f);

    float orbit_yaw = START_CAM_ORBIT_YAW * K_DEG_TO_RAD;
    float elevation = START_CAM_ELEVATION * K_DEG_TO_RAD;
    Vec3 offset = vec3_create(
        START_CAM_DISTANCE * sinf(orbit_yaw) * cosf(elevation),
        START_CAM_DISTANCE * sinf(elevation),
        START_CAM_DISTANCE * cosf(orbit_yaw) * cosf(elevation));

    FreeCamera cam;
    cam.pos = vec3_add(center, offset);
    cam.yaw = START_CAM_YAW;
    cam.pitch = START_CAM_PITCH;
    return cam;
}

enum class AppState : uint8_t
{
    Menu,
    Playing,
    Paused
};

enum class ActiveScene : uint8_t
{
    None,
    World
};

/* Command-line numbers must parse completely: a typo fails the run instead of quietly
 * becoming 0 */
static bool parse_int_arg(const char *text, int *out)
{
    char *end = nullptr;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value < INT_MIN || value > INT_MAX)
        return false;
    *out = static_cast<int>(value);
    return true;
}

static bool parse_float_arg(const char *text, float *out)
{
    char *end = nullptr;
    errno = 0;
    const float value = strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !std::isfinite(value))
        return false;
    *out = value;
    return true;
}

int patch_main(int argc, char *argv[])
{
    /* An unbuffered write per printf costs up to milliseconds when stdout is redirected; the
     * main loop flushes once per frame, so harnesses still read the log as it happens.
     * stderr stays unbuffered. */
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    setvbuf(stderr, NULL, _IONBF, 0);

    int test_scene = -1;
    int test_frames = 0;
    const char *profile_csv = "profile_results.csv";
    bool has_camera_pos = false;
    float camera_pos_x = 0.0f, camera_pos_y = 0.0f, camera_pos_z = 0.0f;
    bool has_camera_approach = false;
    float camera_start[3] = {0.0f, 0.0f, 0.0f};
    float camera_end[3] = {0.0f, 0.0f, 0.0f};
    bool has_camera_target = false;
    float camera_target[3] = {0.0f, 0.0f, 0.0f};
    float test_time_of_day = -1.0f;
    int test_weather = -1;
    DestructionScript destruction_script = {};
    WindowMode window_mode = WindowMode::Fullscreen;
    int32_t windowed_width = 0;
    int32_t windowed_height = 0;
    bool test_resize = false;

    /* Reads the value after argv[i] into *out; false (after an error message) when it is
     * missing or does not parse */
    auto int_value = [&](int &i, int *out) {
        if (i + 1 < argc && parse_int_arg(argv[i + 1], out))
        {
            i++;
            return true;
        }
        fprintf(stderr, "Invalid or missing integer after %s\n", argv[i]);
        return false;
    };
    auto float_values = [&](int &i, float *out, int count) {
        const char *flag = argv[i];
        for (int k = 0; k < count; k++)
        {
            if (i + 1 >= argc || !parse_float_arg(argv[i + 1], &out[k]))
            {
                fprintf(stderr, "Invalid or missing number %d after %s\n", k + 1, flag);
                return false;
            }
            i++;
        }
        return true;
    };
    /* An optional value follows when the next argument is not another flag */
    auto has_value = [&](int i) { return i + 1 < argc && argv[i + 1][0] != '-'; };

    for (int i = 1; i < argc; i++)
    {
        bool ok = true;
        if (strcmp(argv[i], "--scene") == 0)
        {
            ok = int_value(i, &test_scene);
        }
        else if (strcmp(argv[i], "--test-frames") == 0)
        {
            ok = int_value(i, &test_frames);
        }
        else if (strcmp(argv[i], "--profile-csv") == 0 && i + 1 < argc)
        {
            profile_csv = argv[++i];
        }
        else if (strcmp(argv[i], "--camera-pos") == 0)
        {
            float pos[3] = {};
            ok = float_values(i, pos, 3);
            camera_pos_x = pos[0];
            camera_pos_y = pos[1];
            camera_pos_z = pos[2];
            has_camera_pos = ok;
        }
        else if (strcmp(argv[i], "--camera-approach") == 0)
        {
            float path[6] = {};
            ok = float_values(i, path, 6);
            memcpy(camera_start, path, sizeof(camera_start));
            memcpy(camera_end, path + 3, sizeof(camera_end));
            has_camera_approach = ok;
        }
        else if (strcmp(argv[i], "--camera-target") == 0)
        {
            ok = float_values(i, camera_target, 3);
            has_camera_target = ok;
        }
        else if (strcmp(argv[i], "--time-of-day") == 0)
        {
            ok = float_values(i, &test_time_of_day, 1);
        }
        else if (strcmp(argv[i], "--weather") == 0)
        {
            ok = int_value(i, &test_weather);
        }
        else if (strcmp(argv[i], "--test-destruction") == 0)
        {
            int interval = 10; /* frames between destructions */
            if (has_value(i))
                ok = int_value(i, &interval);
            /* 0: every frame (0 would turn the script off) */
            destruction_script.interval_frames = interval < 1 ? 1 : interval;
        }
        else if (strcmp(argv[i], "--windowed") == 0)
        {
            window_mode = WindowMode::Windowed;
            /* Optional client size in pixels */
            if (has_value(i))
                ok = int_value(i, &windowed_width) && int_value(i, &windowed_height);
        }
        else if (strcmp(argv[i], "--test-resize") == 0)
        {
            test_resize = true;
        }
        if (!ok)
            return EXIT_INVALID_ARGUMENT;
    }

    if (test_scene >= SCENE_TYPE_COUNT)
    {
        fprintf(stderr, "Invalid scene ID: %d (max: %d)\n", test_scene, SCENE_TYPE_COUNT - 1);
        return EXIT_INVALID_SCENE;
    }

    printf("Patch\n\n");

    uint64_t rng_seed_value = DEFAULT_RNG_SEED;
    const char *seed_env = getenv("PATCH_RNG_SEED");
    if (seed_env)
    {
        rng_seed_value = static_cast<uint64_t>(strtoull(seed_env, nullptr, 10));
    }

    Window window("Patch", test_frames > 0 ? WindowMode::Test : window_mode, windowed_width, windowed_height);
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
        renderer.set_material_palette(materials, g_material_count);
    }

    bool window_shown = false;

    AppUI ui;
    app_ui_init(&ui);

    AppState app_state = AppState::Menu;
    ActiveScene current_scene = ActiveScene::None;
    Scene *active_scene = nullptr;
    int frames_remaining = test_frames;

    /*
     * Loads a new World scene behind the loading screen: the scene's init stages, then the
     * renderer's terrain stages, with a loading screen frame between polls. The bar runs once
     * over both, each stage taking an equal share. False when the window closed first: the
     * caller leaves the half-built scene to the end of the main loop, which destroys it.
     */
    auto load_world_scene = [&](Scene *scene) -> bool
    {
        constexpr float SCENE_SHARE = static_cast<float>(WORLD_SCENE_INIT_STAGES) /
                                      static_cast<float>(WORLD_SCENE_INIT_STAGES + Renderer::SCENE_LOAD_STAGES);
        LoadingState scene_state;
        LoadingState terrain_state;
        loading_state_init(&scene_state, WORLD_SCENE_INIT_STAGES);
        loading_state_init(&terrain_state, Renderer::SCENE_LOAD_STAGES);
        bool scene_loaded = false;
        while (!window.should_close())
        {
            if (!scene_loaded)
            {
                scene_loaded = world_scene_init_step(scene, &scene_state);
                /* Without a terrain (out of memory) there is nothing to upload */
                VoxelVolume *terrain = scene_loaded ? world_scene_get_terrain(scene) : nullptr;
                if (terrain)
                    renderer.begin_scene_load(terrain, VOBJ_MAX_OBJECTS, &terrain_state);
                else if (scene_loaded)
                    break;
            }
            else if (renderer.poll_scene_load(&terrain_state))
            {
                break;
            }

            window.poll_events();
            if (window.consume_resize())
                renderer.on_resize();
            LoadingState shown = scene_loaded ? terrain_state : scene_state;
            shown.progress = scene_loaded ? SCENE_SHARE + (1.0f - SCENE_SHARE) * terrain_state.progress
                                          : SCENE_SHARE * scene_state.progress;
            uint32_t li = 0;
            /* Skipped while minimized or while the swapchain is being rebuilt */
            if (renderer.begin_frame(&li))
            {
                renderer.begin_main_pass(li);
                patch::render_loading_screen(renderer, &shown, window.width(), window.height());
                renderer.end_frame(li);
            }
            fflush(stdout);
            if (renderer.quit_requested())
                window.request_close();
            if (!window_shown)
            {
                window.show();
                window_shown = true;
            }
        }
        return !window.should_close();
    };

    if (test_scene >= 0)
    {
        const SceneDescriptor *desc = nullptr;
        switch (test_scene)
        {
        case SCENE_TYPE_WORLD:
            desc = scene_get_descriptor(SCENE_TYPE_WORLD);
            active_scene = world_scene_create(desc->bounds, desc->voxel_size, nullptr);
            current_scene = ActiveScene::World;
            break;
        default:
            break;
        }
        if (active_scene)
        {
            rng_seed(&active_scene->rng, rng_seed_value++);
            /* A fading menu blocks gameplay for a wall-clock time, which would make what a
             * test run simulates depend on the frame rate */
            app_ui_hide_now(&ui);
            app_state = AppState::Playing;
            load_world_scene(active_scene);

            renderer.set_scene_lighting(&desc->lighting);

            if (current_scene == ActiveScene::World)
            {
                /* Test runs can start at a time of day and in a weather (lookdev captures) */
                if (test_time_of_day >= 0.0f)
                    world_scene_set_time_of_day(active_scene, test_time_of_day);
                if (test_weather >= 0)
                    world_scene_set_weather(active_scene, test_weather);
            }

            printf("Test mode: scene %d (%s), frames %d\n", test_scene, desc->name, test_frames);
        }
    }

    platform_time_init();
    PlatformTime last_time = platform_time_now();

    /* Warmup period: skip first N frames for profiling to exclude initialization spike */
    static constexpr int WARMUP_FRAMES = 5;
    int warmup_remaining = (test_frames > 0) ? WARMUP_FRAMES : 0;

    /* Camera setup: the free camera, unless test mode asks for a fixed or moving test camera
     * aimed at the scene center */
    renderer.set_perspective(DEFAULT_FOV, DEFAULT_NEAR, DEFAULT_FAR);

    bool test_camera_active = false;
    bool test_camera_approach = false;
    Vec3 test_camera_pos = vec3_create(0.0f, 0.0f, 0.0f);
    Vec3 test_camera_start = vec3_create(0.0f, 0.0f, 0.0f);
    Vec3 test_camera_end = vec3_create(0.0f, 0.0f, 0.0f);
    int frames_elapsed = 0;
    int test_resizes = 0;
    bool test_failed = false;

    if (test_frames > 0 && has_camera_approach)
    {
        test_camera_start = vec3_create(camera_start[0], camera_start[1], camera_start[2]);
        test_camera_end = vec3_create(camera_end[0], camera_end[1], camera_end[2]);
        test_camera_pos = test_camera_start;
        test_camera_active = true;
        test_camera_approach = true;
        printf("Test mode: camera approach (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f) over %d frames\n",
               camera_start[0], camera_start[1], camera_start[2],
               camera_end[0], camera_end[1], camera_end[2], test_frames);
    }
    else if (test_frames > 0 && has_camera_pos)
    {
        test_camera_pos = vec3_create(camera_pos_x, camera_pos_y, camera_pos_z);
        test_camera_active = true;
        printf("Test mode: camera at (%.1f, %.1f, %.1f)\n", camera_pos_x, camera_pos_y, camera_pos_z);
    }

    {
        SceneLighting menu_lighting = scene_lighting_default();
        menu_lighting.sky_color = vec3_create(0.05f, 0.07f, 0.10f);
        renderer.set_scene_lighting(&menu_lighting);
    }

    bool escape_was_down = false;
    bool f2_was_down = false;
    bool f3_was_down = false;
    bool f4_was_down = false;
    bool f5_was_down = false;
    bool mouse_was_down = false;
    bool overlay_press = false; /* the left button went down on an F2 overlay button */
    bool show_overlay = false;
    int32_t dbg_total_uploaded = 0;
    DebugInfo dbg_info = {};
    DebugExportFeedback dbg_feedback = {};

    /* Debug info (scene-agnostic), gathered only for the overlay and the reports */
    auto populate_debug_info = [&]
    {
        debug_info_clear(&dbg_info);
        VoxelVolume *dbg_terrain = nullptr;
        VoxelObjectWorld *dbg_objects = nullptr;
        ParticleSystem *dbg_particles = nullptr;

        if (active_scene && current_scene == ActiveScene::World)
        {
            dbg_info.scene_name = "World";
            dbg_terrain = world_scene_get_terrain(active_scene);
            dbg_objects = world_scene_get_objects(active_scene);
            dbg_particles = world_scene_get_particles(active_scene);
            const WorldSceneStats *stats = world_scene_get_stats(active_scene);
            if (stats)
            {
                dbg_info.spawn_count = stats->spawn_count;
                dbg_info.tick_count = stats->tick_count;
            }
        }

        debug_info_populate_volume(&dbg_info, dbg_terrain);
        debug_info_populate_objects(&dbg_info, dbg_objects);
        debug_info_populate_particles(&dbg_info, dbg_particles);
        debug_info_populate_renderer(&dbg_info, &renderer);
        debug_info_populate_profiler(&dbg_info);
        dbg_info.total_uploaded = dbg_total_uploaded;
    };

    /* Free camera: reset to the start pose whenever a scene starts */
    FreeCamera free_camera = {vec3_zero(), START_CAM_YAW, START_CAM_PITCH};
    if (active_scene)
        free_camera = free_camera_start_pose(active_scene->bounds);

    /* Right button: a click throws a shape, a drag past RIGHT_CLICK_MAX_MOVE_PX looks around */
    bool right_was_down = false;
    bool right_dragging = false;
    float right_press_x = 0.0f;
    float right_press_y = 0.0f;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    while (!window.should_close())
    {
        PROFILE_BEGIN(PROFILE_FRAME_TOTAL);

        PlatformTime current_time = platform_time_now();
        float raw_dt = platform_time_delta_seconds(last_time, current_time);
        last_time = current_time;

        float dt = raw_dt;
        if (dt > MAX_FRAME_DT)
            dt = MAX_FRAME_DT;

        PROFILE_BEGIN(PROFILE_POLL_EVENTS);
        window.poll_events();
        PROFILE_END(PROFILE_POLL_EVENTS);

        /* Every WM_SIZE (including minimize to 0x0 and restore) is forwarded; the renderer
         * compares the surface extent in begin_frame and skips frames while it is 0x0. */
        if (window.consume_resize())
        {
            renderer.on_resize();
        }

        bool escape_down = window.keys().escape;
        bool escape_pressed = escape_down && !escape_was_down;
        escape_was_down = escape_down;

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
            renderer.DEBUG_set_terrain_debug_mode((mode + 1) % TERRAIN_DEBUG_MODE_COUNT);
        }

        /* F5: Terrain debug mode previous */
        bool f5_down = window.keys().f5;
        bool f5_pressed = f5_down && !f5_was_down;
        f5_was_down = f5_down;
        if (f5_pressed)
        {
            int mode = renderer.DEBUG_get_terrain_debug_mode();
            renderer.DEBUG_set_terrain_debug_mode((mode + TERRAIN_DEBUG_MODE_COUNT - 1) % TERRAIN_DEBUG_MODE_COUNT);
        }

        /* Left button: a press on an F2 overlay button (as drawn last frame, from last frame's
         * info) is the overlay's until its release, the click; the scene never sees it */
        const bool mouse_down = window.mouse().left_down;
        const bool mouse_released = !mouse_down && mouse_was_down;
        if (mouse_down && !mouse_was_down)
            overlay_press = show_overlay && active_scene &&
                            debug_overlay_hit(window.width(), window.height(), &dbg_info, window.mouse().x, window.mouse().y);
        mouse_was_down = mouse_down;
        const bool overlay_clicked = overlay_press && mouse_released;
        const bool scene_left_down = mouse_down && !overlay_press;
        if (mouse_released)
            overlay_press = false;

        /* Update feedback timer */
        if (dbg_feedback.timer > 0.0f)
            dbg_feedback.timer -= dt;

        bool do_export = f3_pressed;

        PROFILE_BEGIN(PROFILE_UI_UPDATE);
        app_ui_update(&ui, dt, window.mouse().x, window.mouse().y,
                      window.mouse().left_down, window.width(), window.height());
        PROFILE_END(PROFILE_UI_UPDATE);

        AppAction action = app_ui_get_action(&ui);
        switch (action)
        {
        case APP_ACTION_START_WORLD:
        {
            if (active_scene)
            {
                renderer.prepare_for_scene_switch();
                scene_destroy(active_scene);
            }
            const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
            active_scene = world_scene_create(desc->bounds, desc->voxel_size, nullptr);
            rng_seed(&active_scene->rng, rng_seed_value++);

            /* Closed while loading: straight to the end of the main loop, which destroys
             * the half-built scene (the rest of this frame would use it) */
            if (!load_world_scene(active_scene))
                continue;

            current_scene = ActiveScene::World;
            app_state = AppState::Playing;
            app_ui_hide(&ui);
            free_camera = free_camera_start_pose(active_scene->bounds);
            renderer.set_scene_lighting(&desc->lighting);

            printf("Started: %s\n", scene_get_name(active_scene));
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
                /* By game state, not previous_screen: a visit to GRAPHICS overwrites that */
                app_ui_show_screen(&ui, app_state == AppState::Paused ? APP_SCREEN_PAUSE : APP_SCREEN_MAIN_MENU);
            }
            else
            {
                app_ui_show_screen(&ui, ui.previous_screen != APP_SCREEN_NONE ? ui.previous_screen : APP_SCREEN_MAIN_MENU);
            }
            break;

        case APP_ACTION_MAIN_MENU:
            if (active_scene)
            {
                renderer.prepare_for_scene_switch();
                scene_destroy(active_scene);
                active_scene = nullptr;
                current_scene = ActiveScene::None;
            }
            app_state = AppState::Menu;
            app_ui_show_screen(&ui, APP_SCREEN_MAIN_MENU);
            {
                SceneLighting menu_lighting = scene_lighting_default();
                menu_lighting.sky_color = vec3_create(0.05f, 0.07f, 0.10f);
                renderer.set_scene_lighting(&menu_lighting);
            }
            break;

        case APP_ACTION_QUIT:
            window.request_close();
            break;

        default:
            break;
        }

        const bool gameplay_input = app_state == AppState::Playing && active_scene != nullptr && !app_ui_is_blocking(&ui);

        /* Right button, gameplay only (the window captures the mouse while it is down, so the
         * release always arrives): moving past RIGHT_CLICK_MAX_MOVE_PX turns it into a look drag
         * (cursor hidden), a release before that is a click. Leaving gameplay mid-press ends it
         * without a click. */
        bool right_clicked = false;
        {
            const MouseState &mouse = window.mouse();
            const bool right_down = gameplay_input && mouse.right_down;
            const float dx = mouse.x - right_press_x;
            const float dy = mouse.y - right_press_y;
            const bool moved = dx * dx + dy * dy > RIGHT_CLICK_MAX_MOVE_PX * RIGHT_CLICK_MAX_MOVE_PX;
            if (right_down && !right_was_down)
            {
                right_press_x = last_mouse_x = mouse.x;
                right_press_y = last_mouse_y = mouse.y;
            }
            else if (right_down && !right_dragging && moved)
            {
                right_dragging = true;
                window.set_cursor_visible(false);
            }
            else if (!right_down && right_was_down)
            {
                right_clicked = gameplay_input && !right_dragging && !moved;
                if (right_dragging)
                    window.set_cursor_visible(true);
                right_dragging = false;
            }
            right_was_down = right_down;
        }

        if (gameplay_input)
        {
            /* PROFILE_SIM_TICK is recorded per fixed tick inside the scene (world_scene_tick) */
            Vec3 ray_origin, ray_dir;
            renderer.screen_to_ray(window.mouse().x, window.mouse().y, &ray_origin, &ray_dir);

            if (current_scene == ActiveScene::World)
            {
                world_scene_set_ray(active_scene, ray_origin, ray_dir);
                /* The scene throws on a rising edge of its right-button input: a recognised
                 * click arrives as a one-frame press, a look drag never does */
                scene_handle_input(active_scene, window.mouse().x, window.mouse().y,
                                   scene_left_down, right_clicked);
            }

            scene_update(active_scene, dt);

            if (current_scene == ActiveScene::World)
            {
                const SceneLighting *scene_lighting = world_scene_get_lighting(active_scene);
                if (scene_lighting)
                    renderer.set_scene_lighting(scene_lighting);

                destruction_script_frame(&destruction_script, active_scene, frames_elapsed);
            }
        }

        if (test_camera_active && active_scene)
        {
            /* Test mode: fixed or interpolated camera looking at --camera-target, or else at
             * the scene center */
            Vec3 center = has_camera_target
                              ? vec3_create(camera_target[0], camera_target[1], camera_target[2])
                              : vec3_create((active_scene->bounds.min_x + active_scene->bounds.max_x) * 0.5f,
                                            active_scene->bounds.min_y + 2.0f,
                                            (active_scene->bounds.min_z + active_scene->bounds.max_z) * 0.5f);

            if (test_camera_approach && test_frames > 0)
            {
                float t = static_cast<float>(frames_elapsed) / static_cast<float>(test_frames);
                if (t > 1.0f)
                    t = 1.0f;
                /* Linear interpolation between start and end camera positions */
                test_camera_pos.x = test_camera_start.x + (test_camera_end.x - test_camera_start.x) * t;
                test_camera_pos.y = test_camera_start.y + (test_camera_end.y - test_camera_start.y) * t;
                test_camera_pos.z = test_camera_start.z + (test_camera_end.z - test_camera_start.z) * t;
            }

            renderer.set_look_at(test_camera_pos, center);
        }
        else
        {
            /* Free camera: right-drag to look, WASD / Space / Shift to move (gameplay only) */
            if (right_dragging)
            {
                float dx = window.mouse().x - last_mouse_x;
                float dy = window.mouse().y - last_mouse_y;
                last_mouse_x = window.mouse().x;
                last_mouse_y = window.mouse().y;

                free_camera.yaw -= dx * FREE_CAM_SENSITIVITY;
                free_camera.pitch -= dy * FREE_CAM_SENSITIVITY;

                if (free_camera.pitch > FREE_CAM_PITCH_LIMIT)
                    free_camera.pitch = FREE_CAM_PITCH_LIMIT;
                if (free_camera.pitch < -FREE_CAM_PITCH_LIMIT)
                    free_camera.pitch = -FREE_CAM_PITCH_LIMIT;
            }

            float yaw_rad = free_camera.yaw * 3.14159f / 180.0f;
            float pitch_rad = free_camera.pitch * 3.14159f / 180.0f;

            Vec3 forward;
            forward.x = cosf(pitch_rad) * sinf(yaw_rad);
            forward.y = sinf(pitch_rad);
            forward.z = cosf(pitch_rad) * cosf(yaw_rad);

            if (gameplay_input)
            {
                Vec3 right = vec3_cross(forward, vec3_create(0.0f, 1.0f, 0.0f));
                right = vec3_normalize(right);

                const float move_speed = FREE_CAM_MOVE_SPEED * dt;
                if (window.keys().w)
                    free_camera.pos = vec3_add(free_camera.pos, vec3_scale(forward, move_speed));
                if (window.keys().s)
                    free_camera.pos = vec3_sub(free_camera.pos, vec3_scale(forward, move_speed));
                if (window.keys().d)
                    free_camera.pos = vec3_add(free_camera.pos, vec3_scale(right, move_speed));
                if (window.keys().a)
                    free_camera.pos = vec3_sub(free_camera.pos, vec3_scale(right, move_speed));
                if (window.keys().space)
                    free_camera.pos.y += move_speed;
                if (window.keys().shift)
                    free_camera.pos.y -= move_speed;
            }

            Vec3 target = vec3_add(free_camera.pos, forward);
            renderer.set_look_at(free_camera.pos, target);
        }

        uint32_t image_index = 0;
        /* false while minimized, while the swapchain is being rebuilt, or after a fatal
         * renderer error: nothing may be recorded or submitted this iteration */
        const bool frame_ok = renderer.begin_frame(&image_index);
        if (!frame_ok && renderer.quit_requested())
            window.request_close();

        const AppSettings *settings = app_ui_get_settings(&ui);
        AppSettings test_settings;
        if (test_frames > 0)
        {
            test_settings = *settings;
            test_settings.master_preset = QUALITY_PRESET_FAIR;
            test_settings.adaptive_quality = 0;
            test_settings.shadow_quality = QUALITY_PRESETS[QUALITY_PRESET_FAIR].shadow;
            test_settings.shadow_contact_hardening = QUALITY_PRESETS[QUALITY_PRESET_FAIR].shadow_contact;
            test_settings.ao_quality = QUALITY_PRESETS[QUALITY_PRESET_FAIR].ao;
            test_settings.denoise_quality = QUALITY_PRESETS[QUALITY_PRESET_FAIR].denoise;
            test_settings.taa_quality = QUALITY_PRESETS[QUALITY_PRESET_FAIR].taa;
            settings = &test_settings;
        }

        renderer.set_master_preset(settings->master_preset);
        renderer.set_adaptive_quality(settings->adaptive_quality != 0);

        if (!renderer.get_adaptive_quality())
        {
            if (settings->master_preset < QUALITY_PRESET_CUSTOM)
            {
                renderer.apply_preset(settings->master_preset);
            }
            else
            {
                renderer.set_shadow_quality(settings->shadow_quality);
                renderer.set_shadow_contact_hardening(settings->shadow_contact_hardening != 0);
                renderer.set_ao_quality(settings->ao_quality);
                renderer.set_denoise_quality(settings->denoise_quality);
                renderer.set_taa_quality(settings->taa_quality);
            }
        }

        PROFILE_BEGIN(PROFILE_RENDER_TOTAL);

        PROFILE_BEGIN(PROFILE_RENDER_MAIN);

        if (frame_ok && active_scene && current_scene == ActiveScene::World)
        {
            VoxelVolume *terrain = world_scene_get_terrain(active_scene);
            VoxelObjectWorld *objects = world_scene_get_objects(active_scene);
            ParticleSystem *particles = world_scene_get_particles(active_scene);
            EnvParticleSystem *env_particles = world_scene_get_env_particles(active_scene);

            /* Objects and particles are drawn between their last two simulation steps */
            {
                const WorldSceneStats *stats = world_scene_get_stats(active_scene);
                renderer.set_interp_alpha(scene_interp_alpha(active_scene), stats ? stats->tick_count : 0);
            }

            if (terrain)
            {
                PROFILE_BEGIN(PROFILE_RENDER_VOLUME_BEGIN);
                volume_begin_frame(terrain);
                PROFILE_END(PROFILE_RENDER_VOLUME_BEGIN);

                PROFILE_BEGIN(PROFILE_RENDER_CHUNK_UPLOAD);
                int32_t dirty_indices[VOLUME_MAX_DIRTY_PER_FRAME];
                int32_t uploaded = renderer.upload_dirty_chunks(terrain, dirty_indices, VOLUME_MAX_DIRTY_PER_FRAME);
                if (uploaded > 0)
                {
                    dbg_total_uploaded += uploaded;
                    volume_mark_chunks_uploaded(terrain, dirty_indices, uploaded);
                }
                PROFILE_END(PROFILE_RENDER_CHUNK_UPLOAD);

                PROFILE_BEGIN(PROFILE_RENDER_SHADOW_VOLUME);
                renderer.update_shadow_volume(terrain);
                PROFILE_END(PROFILE_RENDER_SHADOW_VOLUME);

                PROFILE_BEGIN(PROFILE_RENDER_GBUFFER);
                renderer.prepare_gbuffer_compute();
                PROFILE_END(PROFILE_RENDER_GBUFFER);
            }

            renderer.begin_gbuffer_pass();

            if (objects)
            {
                PROFILE_BEGIN(PROFILE_RENDER_OBJECTS);
                renderer.render_voxel_objects_raymarched(objects);
                PROFILE_END(PROFILE_RENDER_OBJECTS);
            }

            if (particles || env_particles)
            {
                renderer.render_particles_raymarched(particles, env_particles);
            }

            renderer.end_gbuffer_pass();
            PROFILE_BEGIN(PROFILE_RENDER_LIGHTING);
            renderer.render_deferred_lighting(image_index);
            PROFILE_END(PROFILE_RENDER_LIGHTING);
        }
        else if (frame_ok)
        {
            renderer.begin_main_pass(image_index);
        }
        PROFILE_END(PROFILE_RENDER_MAIN);

        if (show_overlay || do_export)
            populate_debug_info();

        DebugOverlayActions dbg_actions = {};
        dbg_actions.time_preset = -1;
        dbg_actions.weather_preset = -1;
        bool render_ui = show_overlay || app_ui_is_blocking(&ui);
        if (frame_ok && show_overlay && active_scene)
        {
            PROFILE_BEGIN(PROFILE_RENDER_UI_OVERLAY);
            draw_debug_overlay(renderer,
                               window.width(), window.height(),
                               &dbg_info,
                               window.mouse().x, window.mouse().y,
                               overlay_clicked, &dbg_feedback, &dbg_actions);
            PROFILE_END(PROFILE_RENDER_UI_OVERLAY);
        }

        if (active_scene && current_scene == ActiveScene::World)
        {
            static const float time_presets[] = {0.25f, 0.5f, 0.75f, 0.0f};
            if (dbg_actions.time_preset >= 0 && dbg_actions.time_preset < 4)
                world_scene_set_time_of_day(active_scene, time_presets[dbg_actions.time_preset]);
            if (dbg_actions.weather_preset >= 0)
                world_scene_set_weather(active_scene, dbg_actions.weather_preset);
        }

        if (active_scene && (do_export || dbg_actions.export_clicked))
        {
            const char *report_filename = "debug_report.txt";
            dbg_feedback.success = export_debug_report(report_filename, &dbg_info);
            snprintf(dbg_feedback.filename, sizeof(dbg_feedback.filename), "%s", report_filename);
            dbg_feedback.timer = 3.0f;
        }

        if (frame_ok && render_ui)
        {
            PROFILE_BEGIN(PROFILE_RENDER_UI);
            ui_render(&ui.ctx, app_ui_get_active_menu(&ui), renderer, window.width(), window.height());
            PROFILE_END(PROFILE_RENDER_UI);
        }

        PROFILE_END(PROFILE_RENDER_TOTAL);

        if (frame_ok)
            renderer.end_frame(image_index);
        else if (!window.should_close())
            Sleep(5); /* minimized: don't spin a core at 100% */

        if (!window_shown)
        {
            window.show();
            window_shown = true;
        }

        if (test_frames > 0 && frame_ok)
        {
            /* Warmup period: reset profiling after first few frames to exclude init spike */
            if (warmup_remaining > 0)
            {
                warmup_remaining--;
                if (warmup_remaining == 0)
                {
#ifdef PATCH_PROFILE
                    profile_reset_all();
#endif
                }
            }
            else
            {
                frames_remaining--;
                frames_elapsed++;
            }

            /* The size requested last must be the one the frames since rendered at */
            if (test_resize && warmup_remaining <= 0 && frames_elapsed % TEST_RESIZE_INTERVAL == 0)
            {
                constexpr int SIZE_COUNT = static_cast<int>(sizeof(TEST_RESIZE_SIZES) / sizeof(TEST_RESIZE_SIZES[0]));
                const VkExtent2D extent = renderer.get_render_extent();
                if (test_resizes > 0)
                {
                    const int32_t *requested = TEST_RESIZE_SIZES[(test_resizes - 1) % SIZE_COUNT];
                    if (static_cast<int32_t>(extent.width) != requested[0] || static_cast<int32_t>(extent.height) != requested[1])
                    {
                        fprintf(stderr, "Test mode: rendered at %ux%u after a resize to %dx%d\n",
                                extent.width, extent.height, requested[0], requested[1]);
                        test_failed = true;
                        window.request_close();
                    }
                }
                const int32_t *next = TEST_RESIZE_SIZES[test_resizes % SIZE_COUNT];
                window.set_client_size(next[0], next[1]);
                test_resizes++;
            }

            if (frames_remaining <= 0 && warmup_remaining <= 0)
            {
                printf("Test mode: completed %d frames\n", test_frames);
                if (destruction_script.interval_frames > 0)
                    printf("Test mode: %d scripted destructions removed %d voxels\n",
                           destruction_script.destructions, destruction_script.voxels_removed);
                if (test_resize)
                    printf("Test mode: %d window resizes\n", test_resizes);
                PROFILE_END(PROFILE_FRAME_TOTAL);
                PROFILE_FRAME_END();
                populate_debug_info();
                export_debug_report(profile_csv, &dbg_info);
                printf("Debug report exported to %s\n", profile_csv);
                break;
            }
        }

        PROFILE_END(PROFILE_FRAME_TOTAL);
        PROFILE_FRAME_END();

        renderer.update_adaptive_quality();
        fflush(stdout);
    }

    if (active_scene)
    {
        /* A load still running reads the scene */
        renderer.prepare_for_scene_switch();
        scene_destroy(active_scene);
    }

    /* A renderer failure or a closed window ends a test run early: it must not pass */
    if (test_frames > 0 && (frames_remaining > 0 || test_failed))
        return EXIT_TEST_FAILED;
    return 0;
}

int main(int argc, char *argv[])
{
    return patch_main(argc, argv);
}
