#ifndef PATCH_CORE_UI_H
#define PATCH_CORE_UI_H

#include "types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MAX_MENU_ITEMS 16
#define UI_MAX_TEXT_LEN 64

typedef enum {
    UI_SCREEN_NONE,
    UI_SCREEN_MAIN_MENU,
    UI_SCREEN_PAUSE,
    UI_SCREEN_SCENE_SELECT,
    UI_SCREEN_SETTINGS
} UIScreen;

typedef enum {
    UI_ITEM_BUTTON,
    UI_ITEM_TOGGLE,
    UI_ITEM_SLIDER,
    UI_ITEM_LABEL
} UIItemType;

typedef struct {
    UIItemType type;
    char text[UI_MAX_TEXT_LEN];
    int32_t action_id;
    bool toggle_state;
    bool enabled;
    bool hovered;
    int32_t slider_value;
    int32_t slider_min;
    int32_t slider_max;
    int32_t slider_step;
} UIMenuItem;

typedef struct {
    char title[UI_MAX_TEXT_LEN];
    UIMenuItem items[UI_MAX_MENU_ITEMS];
    int32_t item_count;
    int32_t selected_index;
} UIMenu;

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_START_BALL_PIT,
    UI_ACTION_START_MELEE,
    UI_ACTION_START_SHOOTER,
    UI_ACTION_RESUME,
    UI_ACTION_MAIN_MENU,
    UI_ACTION_QUIT,
    UI_ACTION_SCENE_SELECT,
    UI_ACTION_SETTINGS,
    UI_ACTION_BACK,
    UI_ACTION_DEAD_BODY_LIMIT
} UIAction;

typedef struct {
    UIScreen current_screen;
    UIScreen previous_screen;
    UIMenu main_menu;
    UIMenu pause_menu;
    UIMenu scene_menu;
    UIMenu settings_menu;
    
    float mouse_x;
    float mouse_y;
    bool mouse_clicked;
    bool mouse_was_down;
    
    UIAction pending_action;
    
    float fade_alpha;
    float fade_target;
    float fade_speed;
    
    int32_t dead_body_limit;
    
    bool visible;
} UIState;

void ui_init(UIState* ui);
void ui_show_screen(UIState* ui, UIScreen screen);
void ui_hide(UIState* ui);
void ui_update(UIState* ui, float dt, float mouse_x, float mouse_y, bool mouse_down, int32_t window_width, int32_t window_height);
UIAction ui_get_pending_action(UIState* ui);
bool ui_is_blocking(const UIState* ui);

UIMenu* ui_get_active_menu(UIState* ui);

#ifdef __cplusplus
}
#endif

#endif
