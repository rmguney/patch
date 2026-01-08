#include "ui.h"
#include <string.h>
#include <math.h>

static void init_main_menu(UIMenu* menu) {
    strcpy(menu->title, "PATCH");
    menu->item_count = 0;
    menu->selected_index = 0;
    
    UIMenuItem* item;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "PLAY");
    item->action_id = UI_ACTION_SCENE_SELECT;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "OPTIONS");
    item->action_id = UI_ACTION_SETTINGS;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "QUIT");
    item->action_id = UI_ACTION_QUIT;
    item->enabled = true;
    item->hovered = false;
}

static void init_pause_menu(UIMenu* menu) {
    strcpy(menu->title, "PAUSED");
    menu->item_count = 0;
    menu->selected_index = 0;
    
    UIMenuItem* item;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "RESUME");
    item->action_id = UI_ACTION_RESUME;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "SCENE");
    item->action_id = UI_ACTION_SCENE_SELECT;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "MAIN MENU");
    item->action_id = UI_ACTION_MAIN_MENU;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "QUIT");
    item->action_id = UI_ACTION_QUIT;
    item->enabled = true;
    item->hovered = false;
}

static void init_scene_menu(UIMenu* menu) {
    strcpy(menu->title, "SCENE");
    menu->item_count = 0;
    menu->selected_index = 0;
    
    UIMenuItem* item;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "MELEE");
    item->action_id = UI_ACTION_START_MELEE;
    item->enabled = true;
    item->hovered = false;

    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "SHOOTER");
    item->action_id = UI_ACTION_START_SHOOTER;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "BALL PIT");
    item->action_id = UI_ACTION_START_BALL_PIT;
    item->enabled = true;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_LABEL;
    strcpy(item->text, "");
    item->action_id = UI_ACTION_NONE;
    item->enabled = false;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "BACK");
    item->action_id = UI_ACTION_MAIN_MENU;
    item->enabled = true;
    item->hovered = false;
}

static void init_settings_menu(UIMenu* menu, int32_t dead_body_limit) {
    strcpy(menu->title, "SETTINGS");
    menu->item_count = 0;
    menu->selected_index = 0;
    
    UIMenuItem* item;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_SLIDER;
    strcpy(item->text, "BODY LIMIT");
    item->action_id = UI_ACTION_DEAD_BODY_LIMIT;
    item->enabled = true;
    item->hovered = false;
    item->slider_value = dead_body_limit;
    item->slider_min = 1;
    item->slider_max = 500;
    item->slider_step = 10;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_LABEL;
    strcpy(item->text, "");
    item->action_id = UI_ACTION_NONE;
    item->enabled = false;
    item->hovered = false;
    
    item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    strcpy(item->text, "BACK");
    item->action_id = UI_ACTION_BACK;
    item->enabled = true;
    item->hovered = false;
}

void ui_init(UIState* ui) {
    memset(ui, 0, sizeof(UIState));
    
    ui->current_screen = UI_SCREEN_MAIN_MENU;
    ui->previous_screen = UI_SCREEN_NONE;
    ui->visible = true;
    ui->fade_alpha = 1.0f;
    ui->fade_target = 1.0f;
    ui->fade_speed = 4.0f;
    ui->dead_body_limit = 100;
    
    init_main_menu(&ui->main_menu);
    init_pause_menu(&ui->pause_menu);
    init_scene_menu(&ui->scene_menu);
    init_settings_menu(&ui->settings_menu, ui->dead_body_limit);
}

void ui_show_screen(UIState* ui, UIScreen screen) {
    ui->previous_screen = ui->current_screen;
    ui->current_screen = screen;
    ui->visible = true;
    ui->fade_target = 1.0f;
    
    UIMenu* menu = ui_get_active_menu(ui);
    if (menu) {
        for (int32_t i = 0; i < menu->item_count; i++) {
            menu->items[i].hovered = false;
        }
        menu->selected_index = 0;
    }
}

void ui_hide(UIState* ui) {
    ui->fade_target = 0.0f;
}

static bool point_in_rect(float px, float py, float cx, float cy, float w, float h) {
    float half_w = w * 0.5f;
    float half_h = h * 0.5f;
    return px >= cx - half_w && px <= cx + half_w && py >= cy - half_h && py <= cy + half_h;
}

void ui_update(UIState* ui, float dt, float mouse_x, float mouse_y, bool mouse_down, int32_t window_width, int32_t window_height) {
    ui->mouse_x = mouse_x;
    ui->mouse_y = mouse_y;
    
    bool mouse_clicked = mouse_down && !ui->mouse_was_down;
    ui->mouse_clicked = mouse_clicked;
    ui->mouse_was_down = mouse_down;
    
    if (ui->fade_alpha < ui->fade_target) {
        ui->fade_alpha += ui->fade_speed * dt;
        if (ui->fade_alpha > ui->fade_target) ui->fade_alpha = ui->fade_target;
    } else if (ui->fade_alpha > ui->fade_target) {
        ui->fade_alpha -= ui->fade_speed * dt;
        if (ui->fade_alpha < ui->fade_target) ui->fade_alpha = ui->fade_target;
    }
    
    if (ui->fade_alpha < 0.01f && ui->fade_target < 0.01f) {
        ui->visible = false;
    }
    
    if (!ui->visible) return;
    
    UIMenu* menu = ui_get_active_menu(ui);
    if (!menu) return;
    
    float button_width = 0.4f;
    float button_height = 0.08f;
    float button_spacing = 0.12f;
    float start_y = 0.1f + (float)(menu->item_count - 1) * button_spacing * 0.5f;
    
    float w = (float)window_width;
    float h = (float)window_height;
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
    
    for (int32_t i = 0; i < menu->item_count; i++) {
        UIMenuItem* item = &menu->items[i];
        if (item->type == UI_ITEM_LABEL || !item->enabled) {
            item->hovered = false;
            continue;
        }
        
        float button_cx = 0.0f;
        float button_cy = start_y - (float)i * button_spacing;
        
        float norm_mx = (mouse_x / w) * 2.0f - 1.0f;
        float norm_my = 1.0f - (mouse_y / h) * 2.0f;
        
        item->hovered = point_in_rect(norm_mx, norm_my, button_cx, button_cy, button_width, button_height);
        
        if (item->hovered && mouse_clicked) {
            ui->pending_action = (UIAction)item->action_id;
            
            if (item->type == UI_ITEM_TOGGLE) {
                item->toggle_state = !item->toggle_state;
            }
            
            if (item->type == UI_ITEM_SLIDER) {
                float slider_width = button_width;
                float rel_x = (norm_mx - (button_cx - slider_width * 0.5f)) / slider_width;
                if (rel_x < 0.0f) rel_x = 0.0f;
                if (rel_x > 1.0f) rel_x = 1.0f;
                
                int32_t new_value = item->slider_min + (int32_t)(rel_x * (float)(item->slider_max - item->slider_min));
                new_value = (new_value / item->slider_step) * item->slider_step;
                item->slider_value = new_value;
                
                if (item->action_id == UI_ACTION_DEAD_BODY_LIMIT) {
                    ui->dead_body_limit = new_value;
                }
            }
        }
    }
}

UIAction ui_get_pending_action(UIState* ui) {
    UIAction action = ui->pending_action;
    ui->pending_action = UI_ACTION_NONE;
    return action;
}

bool ui_is_blocking(const UIState* ui) {
    return ui->visible && ui->current_screen != UI_SCREEN_NONE;
}

UIMenu* ui_get_active_menu(UIState* ui) {
    switch (ui->current_screen) {
        case UI_SCREEN_MAIN_MENU:
            return &ui->main_menu;
        case UI_SCREEN_PAUSE:
            return &ui->pause_menu;
        case UI_SCREEN_SCENE_SELECT:
            return &ui->scene_menu;
        case UI_SCREEN_SETTINGS:
            return &ui->settings_menu;
        default:
            return NULL;
    }
}
