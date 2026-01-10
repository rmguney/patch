#include "app_ui.h"
#include <string.h>

static void init_main_menu(UIMenu *menu)
{
    ui_menu_clear(menu, "PATCH");
    ui_menu_add_button(menu, "PLAY", APP_ACTION_SCENE_SELECT);
    ui_menu_add_button(menu, "OPTIONS", APP_ACTION_SETTINGS);
    ui_menu_add_button(menu, "QUIT", APP_ACTION_QUIT);
}

static void init_pause_menu(UIMenu *menu)
{
    ui_menu_clear(menu, "PAUSED");
    ui_menu_add_button(menu, "RESUME", APP_ACTION_RESUME);
    ui_menu_add_button(menu, "OPTIONS", APP_ACTION_SETTINGS);
    ui_menu_add_button(menu, "SCENE", APP_ACTION_SCENE_SELECT);
    ui_menu_add_button(menu, "MAIN MENU", APP_ACTION_MAIN_MENU);
    ui_menu_add_button(menu, "QUIT", APP_ACTION_QUIT);
}

static void init_scene_menu(UIMenu *menu)
{
    ui_menu_clear(menu, "SAMPLES");
    ui_menu_add_button(menu, "BALL PIT", APP_ACTION_START_BALL_PIT);
    ui_menu_add_label(menu, NULL);
    ui_menu_add_button(menu, "BACK", APP_ACTION_MAIN_MENU);
}

static void init_settings_menu(UIMenu *menu, const AppSettings *s)
{
    ui_menu_clear(menu, "OPTIONS");
    ui_menu_add_slider(menu, "INITIAL SPAWNS", APP_ACTION_SETTING_INITIAL_SPAWNS,
                       s->initial_spawns, 1, 100, 5);
    ui_menu_add_slider(menu, "SPAWN INTERVAL MS", APP_ACTION_SETTING_SPAWN_INTERVAL,
                       s->spawn_interval_ms, 100, 2000, 100);
    ui_menu_add_slider(menu, "SPAWN BATCH", APP_ACTION_SETTING_SPAWN_BATCH,
                       s->spawn_batch, 1, 10, 1);
    ui_menu_add_slider(menu, "MAX SPAWNS", APP_ACTION_SETTING_MAX_SPAWNS,
                       s->max_spawns, 50, 1024, 50);
    ui_menu_add_slider(menu, "VOXEL SIZE (MM)", APP_ACTION_SETTING_VOXEL_SIZE,
                       s->voxel_size_mm, 50, 200, 10);
    ui_menu_add_label(menu, NULL);
    ui_menu_add_button(menu, "RUN STRESS TEST", APP_ACTION_RUN_STRESS_TEST);
    ui_menu_add_button(menu, "BACK", APP_ACTION_BACK);
}

void app_ui_init(AppUI *ui)
{
    memset(ui, 0, sizeof(AppUI));

    ui_context_init(&ui->ctx);
    ui->current_screen = APP_SCREEN_MAIN_MENU;
    ui->previous_screen = APP_SCREEN_NONE;

    ui->settings.initial_spawns = 10;
    ui->settings.spawn_interval_ms = 500;
    ui->settings.spawn_batch = 3;
    ui->settings.max_spawns = 1024;
    ui->settings.voxel_size_mm = 100;

    init_main_menu(&ui->main_menu);
    init_pause_menu(&ui->pause_menu);
    init_scene_menu(&ui->scene_menu);
    init_settings_menu(&ui->settings_menu, &ui->settings);
}

void app_ui_show_screen(AppUI *ui, AppScreen screen)
{
    ui->previous_screen = ui->current_screen;
    ui->current_screen = screen;
    ui_context_show(&ui->ctx);

    UIMenu *menu = app_ui_get_active_menu(ui);
    if (menu)
    {
        for (int32_t i = 0; i < menu->item_count; i++)
        {
            menu->items[i].hovered = false;
        }
        menu->selected_index = 0;
    }
}

void app_ui_hide(AppUI *ui)
{
    ui_context_hide(&ui->ctx);
}

static void sync_settings_from_menu(AppUI *ui)
{
    UIMenu *menu = &ui->settings_menu;
    for (int32_t i = 0; i < menu->item_count; i++)
    {
        UIMenuItem *item = &menu->items[i];
        if (item->type != UI_ITEM_SLIDER)
            continue;

        switch (item->action_id)
        {
        case APP_ACTION_SETTING_INITIAL_SPAWNS:
            ui->settings.initial_spawns = item->slider_value;
            break;
        case APP_ACTION_SETTING_SPAWN_INTERVAL:
            ui->settings.spawn_interval_ms = item->slider_value;
            break;
        case APP_ACTION_SETTING_SPAWN_BATCH:
            ui->settings.spawn_batch = item->slider_value;
            break;
        case APP_ACTION_SETTING_MAX_SPAWNS:
            ui->settings.max_spawns = item->slider_value;
            break;
        case APP_ACTION_SETTING_VOXEL_SIZE:
            ui->settings.voxel_size_mm = item->slider_value;
            break;
        }
    }
}

void app_ui_update(AppUI *ui, float dt, float mouse_x, float mouse_y, bool mouse_down,
                   int32_t window_width, int32_t window_height)
{
    ui_context_update(&ui->ctx, dt, mouse_x, mouse_y, mouse_down);

    if (!ui->ctx.visible)
        return;

    UIMenu *menu = app_ui_get_active_menu(ui);
    int32_t action = ui_menu_update(&ui->ctx, menu, window_width, window_height);

    if (ui->current_screen == APP_SCREEN_SETTINGS)
    {
        sync_settings_from_menu(ui);
    }

    if (action != 0)
    {
        ui->ctx.pending_action = action;
    }
}

AppAction app_ui_get_action(AppUI *ui)
{
    AppAction action = (AppAction)ui->ctx.pending_action;
    ui->ctx.pending_action = APP_ACTION_NONE;
    return action;
}

bool app_ui_is_blocking(const AppUI *ui)
{
    return ui->ctx.visible && ui->current_screen != APP_SCREEN_NONE;
}

UIMenu *app_ui_get_active_menu(AppUI *ui)
{
    switch (ui->current_screen)
    {
    case APP_SCREEN_MAIN_MENU:
        return &ui->main_menu;
    case APP_SCREEN_PAUSE:
        return &ui->pause_menu;
    case APP_SCREEN_SCENE_SELECT:
        return &ui->scene_menu;
    case APP_SCREEN_SETTINGS:
        return &ui->settings_menu;
    default:
        return NULL;
    }
}

const AppSettings *app_ui_get_settings(const AppUI *ui)
{
    return &ui->settings;
}

void app_ui_refresh_settings_menu(AppUI *ui)
{
    init_settings_menu(&ui->settings_menu, &ui->settings);
}
