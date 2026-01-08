#ifndef PATCH_ENGINE_SIM_UI_H
#define PATCH_ENGINE_SIM_UI_H

#include "engine/core/types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_MAX_MENU_ITEMS 16
#define UI_MAX_TEXT_LEN 64

typedef enum
{
    UI_ITEM_BUTTON,
    UI_ITEM_TOGGLE,
    UI_ITEM_SLIDER,
    UI_ITEM_LABEL
} UIItemType;

typedef struct
{
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

typedef struct
{
    char title[UI_MAX_TEXT_LEN];
    UIMenuItem items[UI_MAX_MENU_ITEMS];
    int32_t item_count;
    int32_t selected_index;
} UIMenu;

void ui_menu_clear(UIMenu *menu, const char *title);
void ui_menu_add_button(UIMenu *menu, const char *text, int32_t action_id);
void ui_menu_add_label(UIMenu *menu, const char *text);
void ui_menu_add_toggle(UIMenu *menu, const char *text, int32_t action_id, bool initial);
void ui_menu_add_slider(UIMenu *menu, const char *text, int32_t action_id,
                        int32_t value, int32_t min_val, int32_t max_val, int32_t step);

typedef struct
{
    float mouse_x;
    float mouse_y;
    bool mouse_clicked;
    bool mouse_was_down;

    int32_t pending_action;

    float fade_alpha;
    float fade_target;
    float fade_speed;

    bool visible;
} UIContext;

void ui_context_init(UIContext *ctx);
void ui_context_show(UIContext *ctx);
void ui_context_hide(UIContext *ctx);
void ui_context_update(UIContext *ctx, float dt, float mouse_x, float mouse_y, bool mouse_down);
bool ui_context_is_blocking(const UIContext *ctx);

int32_t ui_menu_update(UIContext *ctx, UIMenu *menu, int32_t window_width, int32_t window_height);

#ifdef __cplusplus
}
#endif

#endif
