#include "app_ui.h"
#include "engine/render/voxel_push_constants.h"
#include <string.h>

static const char *const QUALITY_4[] = {"NONE", "FAIR", "GOOD", "HIGH"};
static const char *const TOGGLE_LABELS[] = {"OFF", "ON"};
static const char *const PRESET_LABELS[] = {"DEFAULT", "FAIR", "GOOD", "HIGH", "CUSTOM"};

static void apply_preset_to_settings(AppSettings *s, int32_t preset)
{
    if (preset >= 0 && preset < QUALITY_PRESET_CUSTOM)
    {
        s->shadow_quality = QUALITY_PRESETS[preset].shadow;
        s->shadow_contact_hardening = QUALITY_PRESETS[preset].shadow_contact;
        s->ao_quality = QUALITY_PRESETS[preset].ao;
        s->denoise_quality = QUALITY_PRESETS[preset].denoise;
        s->taa_quality = QUALITY_PRESETS[preset].taa;
    }
}

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
    ui_menu_clear(menu, "SCENES");
    ui_menu_add_button(menu, "WORLD", APP_ACTION_START_WORLD);
    ui_menu_add_label(menu, NULL);
    ui_menu_add_button(menu, "BACK", APP_ACTION_BACK);
}

static void init_settings_menu(UIMenu *menu)
{
    /* No voxel size slider: chunk counts and scene bounds are fixed for 100 mm voxels */
    ui_menu_clear(menu, "OPTIONS");
    ui_menu_add_button(menu, "GRAPHICS", APP_ACTION_GRAPHICS);
    ui_menu_add_button(menu, "BACK", APP_ACTION_BACK);
}

static void init_graphics_menu(UIMenu *menu, const AppSettings *s)
{
    ui_menu_clear(menu, "GRAPHICS");
    bool using_preset = s->master_preset < QUALITY_PRESET_CUSTOM;

    /* Master quality preset - controls all settings below */
    ui_menu_add_slider_labeled(menu, "MASTER QUALITY", APP_ACTION_SETTING_MASTER_PRESET,
                               s->master_preset, 0, 4, PRESET_LABELS, 5);

    /* Adaptive quality - dynamically adjusts within preset tiers */
    ui_menu_add_slider_labeled(menu, "ADAPTIVE QUALITY", APP_ACTION_SETTING_ADAPTIVE,
                               s->adaptive_quality, 0, 1, TOGGLE_LABELS, 2);

    ui_menu_add_label(menu, "--- SHADOWS ---");
    ui_menu_add_slider_labeled(menu, "SHADOW QUALITY", APP_ACTION_SETTING_SHADOW_QUALITY,
                               s->shadow_quality, 0, 3, QUALITY_4, 4);
    menu->items[menu->item_count - 1].enabled = !using_preset;
    ui_menu_add_slider_labeled(menu, "CONTACT HARDENING", APP_ACTION_SETTING_SHADOW_CONTACT,
                               s->shadow_contact_hardening, 0, 1, TOGGLE_LABELS, 2);
    menu->items[menu->item_count - 1].enabled = !using_preset;

    ui_menu_add_label(menu, "--- LIGHTING ---");
    ui_menu_add_slider_labeled(menu, "AMBIENT OCCLUSION", APP_ACTION_SETTING_AO_QUALITY,
                               s->ao_quality, 0, 3, QUALITY_4, 4);
    menu->items[menu->item_count - 1].enabled = !using_preset;

    ui_menu_add_label(menu, "--- UTILITY ---");
    ui_menu_add_slider_labeled(menu, "SPATIAL DENOISE", APP_ACTION_SETTING_DENOISE_QUALITY,
                               s->denoise_quality, 0, 1, TOGGLE_LABELS, 2);
    menu->items[menu->item_count - 1].enabled = !using_preset;
    ui_menu_add_slider_labeled(menu, "TAA", APP_ACTION_SETTING_TAA_QUALITY,
                               s->taa_quality, 0, 1, TOGGLE_LABELS, 2);
    menu->items[menu->item_count - 1].enabled = !using_preset;

    ui_menu_add_label(menu, NULL);
    ui_menu_add_button(menu, "BACK", APP_ACTION_BACK);
}

static void sync_graphics_menu_from_settings(AppUI *ui)
{
    UIMenu *menu = &ui->graphics_menu;
    const AppSettings *s = &ui->settings;
    bool using_preset = s->master_preset < QUALITY_PRESET_CUSTOM;

    for (int32_t i = 0; i < menu->item_count; i++)
    {
        UIMenuItem *item = &menu->items[i];
        if (item->type != UI_ITEM_SLIDER)
            continue;

        switch (item->action_id)
        {
        case APP_ACTION_SETTING_MASTER_PRESET:
            item->slider_value = s->master_preset;
            item->enabled = true;
            break;
        case APP_ACTION_SETTING_ADAPTIVE:
            item->slider_value = s->adaptive_quality;
            item->enabled = true;
            break;
        case APP_ACTION_SETTING_SHADOW_QUALITY:
            item->slider_value = s->shadow_quality;
            item->enabled = !using_preset;
            break;
        case APP_ACTION_SETTING_SHADOW_CONTACT:
            item->slider_value = s->shadow_contact_hardening;
            item->enabled = !using_preset;
            break;
        case APP_ACTION_SETTING_AO_QUALITY:
            item->slider_value = s->ao_quality;
            item->enabled = !using_preset;
            break;
        case APP_ACTION_SETTING_DENOISE_QUALITY:
            item->slider_value = s->denoise_quality;
            item->enabled = !using_preset;
            break;
        case APP_ACTION_SETTING_TAA_QUALITY:
            item->slider_value = s->taa_quality;
            item->enabled = !using_preset;
            break;
        default:
            break;
        }
    }
}

void app_ui_init(AppUI *ui)
{
    memset(ui, 0, sizeof(AppUI));

    ui_context_init(&ui->ctx);
    ui->current_screen = APP_SCREEN_MAIN_MENU;
    ui->previous_screen = APP_SCREEN_NONE;

    ui->settings.master_preset = QUALITY_PRESET_FAIR;
    ui->settings.adaptive_quality = 0;
    apply_preset_to_settings(&ui->settings, QUALITY_PRESET_FAIR);

    init_main_menu(&ui->main_menu);
    init_pause_menu(&ui->pause_menu);
    init_scene_menu(&ui->scene_menu);
    init_settings_menu(&ui->settings_menu);
    init_graphics_menu(&ui->graphics_menu, &ui->settings);
}

void app_ui_show_screen(AppUI *ui, AppScreen screen)
{
    ui->previous_screen = ui->current_screen;
    ui->current_screen = screen;
    ui_context_show(&ui->ctx);

    if (screen == APP_SCREEN_GRAPHICS)
    {
        init_graphics_menu(&ui->graphics_menu, &ui->settings);
    }

    UIMenu *menu = app_ui_get_active_menu(ui);
    if (menu)
    {
        for (int32_t i = 0; i < menu->item_count; i++)
        {
            menu->items[i].hovered = false;
        }
    }
}

void app_ui_hide(AppUI *ui)
{
    ui_context_hide(&ui->ctx);
}

void app_ui_hide_now(AppUI *ui)
{
    ui_context_hide_now(&ui->ctx);
}

static void sync_graphics_from_menu(AppUI *ui)
{
    UIMenu *menu = &ui->graphics_menu;
    bool needs_rebuild = false;
    bool master_preset_changed = false;
    bool individual_changed = false;
    bool allow_individual = ui->settings.master_preset >= QUALITY_PRESET_CUSTOM;

    for (int32_t i = 0; i < menu->item_count; i++)
    {
        UIMenuItem *item = &menu->items[i];
        if (item->type != UI_ITEM_SLIDER)
            continue;

        switch (item->action_id)
        {
        case APP_ACTION_SETTING_MASTER_PRESET:
            if (ui->settings.master_preset != item->slider_value)
            {
                ui->settings.master_preset = item->slider_value;
                if (item->slider_value < QUALITY_PRESET_CUSTOM)
                {
                    apply_preset_to_settings(&ui->settings, item->slider_value);
                }
                master_preset_changed = true;
                needs_rebuild = true;
                allow_individual = ui->settings.master_preset >= QUALITY_PRESET_CUSTOM;
            }
            break;
        case APP_ACTION_SETTING_ADAPTIVE:
            ui->settings.adaptive_quality = item->slider_value;
            break;
        case APP_ACTION_SETTING_SHADOW_QUALITY:
            if (allow_individual && ui->settings.shadow_quality != item->slider_value)
            {
                ui->settings.shadow_quality = item->slider_value;
                individual_changed = true;
            }
            break;
        case APP_ACTION_SETTING_SHADOW_CONTACT:
            if (allow_individual && ui->settings.shadow_contact_hardening != item->slider_value)
            {
                ui->settings.shadow_contact_hardening = item->slider_value;
                individual_changed = true;
            }
            break;
        case APP_ACTION_SETTING_AO_QUALITY:
            if (allow_individual && ui->settings.ao_quality != item->slider_value)
            {
                ui->settings.ao_quality = item->slider_value;
                individual_changed = true;
            }
            break;
        case APP_ACTION_SETTING_DENOISE_QUALITY:
            if (allow_individual && ui->settings.denoise_quality != item->slider_value)
            {
                ui->settings.denoise_quality = item->slider_value;
                individual_changed = true;
            }
            break;
        case APP_ACTION_SETTING_TAA_QUALITY:
            if (allow_individual && ui->settings.taa_quality != item->slider_value)
            {
                ui->settings.taa_quality = item->slider_value;
                individual_changed = true;
            }
            break;
        default:
            break;
        }
    }

    if (individual_changed && !master_preset_changed && ui->settings.master_preset < QUALITY_PRESET_CUSTOM)
    {
        ui->settings.master_preset = QUALITY_PRESET_CUSTOM;
        needs_rebuild = true;
    }

    if (needs_rebuild)
        init_graphics_menu(menu, &ui->settings);

    sync_graphics_menu_from_settings(ui);
}

void app_ui_update(AppUI *ui, float dt, float mouse_x, float mouse_y, bool mouse_down,
                   int32_t window_width, int32_t window_height)
{
    ui_context_update(&ui->ctx, dt, mouse_x, mouse_y, mouse_down);

    /* A menu that is fading out no longer takes clicks */
    if (!ui->ctx.visible || ui->ctx.fade_target <= 0.0f)
        return;

    UIMenu *menu = app_ui_get_active_menu(ui);
    int32_t action = ui_menu_update(&ui->ctx, menu, window_width, window_height);

    if (ui->current_screen == APP_SCREEN_GRAPHICS)
        sync_graphics_from_menu(ui);

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
    case APP_SCREEN_GRAPHICS:
        return &ui->graphics_menu;
    default:
        return NULL;
    }
}

const AppSettings *app_ui_get_settings(const AppUI *ui)
{
    return &ui->settings;
}
