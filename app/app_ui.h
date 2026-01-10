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
    APP_SCREEN_SETTINGS
} AppScreen;

typedef enum
{
    APP_ACTION_NONE = 0,
    APP_ACTION_START_BALL_PIT,
    APP_ACTION_RESUME,
    APP_ACTION_MAIN_MENU,
    APP_ACTION_QUIT,
    APP_ACTION_SCENE_SELECT,
    APP_ACTION_SETTINGS,
    APP_ACTION_BACK,
    APP_ACTION_RUN_STRESS_TEST,
    APP_ACTION_SETTING_INITIAL_SPAWNS = 100,
    APP_ACTION_SETTING_SPAWN_INTERVAL,
    APP_ACTION_SETTING_SPAWN_BATCH,
    APP_ACTION_SETTING_MAX_SPAWNS,
    APP_ACTION_SETTING_VOXEL_SIZE,
    APP_ACTION_SETTING_RT_QUALITY
} AppAction;

typedef struct
{
    int32_t initial_spawns;
    int32_t spawn_interval_ms;
    int32_t spawn_batch;
    int32_t max_spawns;
    int32_t voxel_size_mm;
    int32_t rt_quality; /* 0=Off, 1=Fair, 2=Good, 3=High */
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

    AppSettings settings;
} AppUI;

void app_ui_init(AppUI *ui);
void app_ui_show_screen(AppUI *ui, AppScreen screen);
void app_ui_hide(AppUI *ui);
void app_ui_update(AppUI *ui, float dt, float mouse_x, float mouse_y, bool mouse_down,
                   int32_t window_width, int32_t window_height);
AppAction app_ui_get_action(AppUI *ui);
bool app_ui_is_blocking(const AppUI *ui);
UIMenu *app_ui_get_active_menu(AppUI *ui);
const AppSettings *app_ui_get_settings(const AppUI *ui);
void app_ui_refresh_settings_menu(AppUI *ui);

#ifdef __cplusplus
}
#endif

#endif
