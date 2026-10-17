#ifndef PATCH_APP_UI_H
#define PATCH_APP_UI_H

#include "engine/sim/ui.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        APP_SCREEN_NONE,
        APP_SCREEN_MAIN_MENU,
        APP_SCREEN_PAUSE,
        APP_SCREEN_SCENE_SELECT,
        APP_SCREEN_SETTINGS,
        APP_SCREEN_GRAPHICS
    } AppScreen;

    typedef enum
    {
        APP_ACTION_NONE = 0,
        APP_ACTION_START_WORLD,
        APP_ACTION_RESUME,
        APP_ACTION_MAIN_MENU,
        APP_ACTION_QUIT,
        APP_ACTION_SCENE_SELECT,
        APP_ACTION_SETTINGS,
        APP_ACTION_GRAPHICS,
        APP_ACTION_BACK,
        APP_ACTION_SETTING_ADAPTIVE = 100,
        APP_ACTION_SETTING_SHADOW_QUALITY,
        APP_ACTION_SETTING_SHADOW_CONTACT,
        APP_ACTION_SETTING_AO_QUALITY,
        APP_ACTION_SETTING_DENOISE_QUALITY,
        APP_ACTION_SETTING_TAA_QUALITY,
        APP_ACTION_SETTING_MASTER_PRESET
    } AppAction;

    typedef struct
    {
        int32_t master_preset;            /* 0=Default, 1=Fair, 2=Good, 3=High, 4=Custom */
        int32_t adaptive_quality;         /* 0=Off (default), 1=On */
        int32_t shadow_quality;           /* 0=None, 1=Fair, 2=Good, 3=High */
        int32_t shadow_contact_hardening; /* 0=Off, 1=On */
        int32_t ao_quality;               /* 0=None, 1=Fair, 2=Good, 3=High */
        int32_t denoise_quality;          /* 0=Off, 1=On */
        int32_t taa_quality;              /* 0=Off, 1=On */
    } AppSettings;

    typedef struct
    {
        UIContext ctx;
        AppScreen current_screen;
        AppScreen previous_screen;

        UIMenu main_menu;
        UIMenu pause_menu;
        UIMenu scene_menu;
        UIMenu settings_menu;
        UIMenu graphics_menu;

        AppSettings settings;
    } AppUI;

    void app_ui_init(AppUI *ui);
    void app_ui_show_screen(AppUI *ui, AppScreen screen);
    void app_ui_hide(AppUI *ui);
    void app_ui_hide_now(AppUI *ui);
    void app_ui_update(AppUI *ui, float dt, float mouse_x, float mouse_y, bool mouse_down,
                       int32_t window_width, int32_t window_height);
    AppAction app_ui_get_action(AppUI *ui);
    bool app_ui_is_blocking(const AppUI *ui);
    UIMenu *app_ui_get_active_menu(AppUI *ui);
    const AppSettings *app_ui_get_settings(const AppUI *ui);

#ifdef __cplusplus
}
#endif

#endif
