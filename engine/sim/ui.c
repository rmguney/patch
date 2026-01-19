#include "ui.h"
#include <string.h>
#include <stdio.h>

static inline float clampf_local(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void ui_menu_clear(UIMenu *menu, const char *title)
{
    memset(menu, 0, sizeof(UIMenu));
    if (title)
    {
        snprintf(menu->title, UI_MAX_TEXT_LEN, "%s", title);
    }
}

void ui_menu_add_button(UIMenu *menu, const char *text, int32_t action_id)
{
    if (menu->item_count >= UI_MAX_MENU_ITEMS)
        return;

    UIMenuItem *item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_BUTTON;
    snprintf(item->text, UI_MAX_TEXT_LEN, "%s", text);
    item->action_id = action_id;
    item->enabled = true;
    item->hovered = false;
}

void ui_menu_add_label(UIMenu *menu, const char *text)
{
    if (menu->item_count >= UI_MAX_MENU_ITEMS)
        return;

    UIMenuItem *item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_LABEL;
    if (text)
    {
        snprintf(item->text, UI_MAX_TEXT_LEN, "%s", text);
    }
    else
    {
        item->text[0] = '\0';
    }
    item->action_id = 0;
    item->enabled = false;
    item->hovered = false;
}

void ui_menu_add_toggle(UIMenu *menu, const char *text, int32_t action_id, bool initial)
{
    if (menu->item_count >= UI_MAX_MENU_ITEMS)
        return;

    UIMenuItem *item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_TOGGLE;
    snprintf(item->text, UI_MAX_TEXT_LEN, "%s", text);
    item->action_id = action_id;
    item->toggle_state = initial;
    item->enabled = true;
    item->hovered = false;
}

void ui_menu_add_slider(UIMenu *menu, const char *text, int32_t action_id,
                        int32_t value, int32_t min_val, int32_t max_val, int32_t step)
{
    if (menu->item_count >= UI_MAX_MENU_ITEMS)
        return;

    UIMenuItem *item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_SLIDER;
    snprintf(item->text, UI_MAX_TEXT_LEN, "%s", text);
    item->action_id = action_id;
    item->slider_value = value;
    item->slider_min = min_val;
    item->slider_max = max_val;
    item->slider_step = step;
    item->enabled = true;
    item->hovered = false;
    item->slider_labels = NULL;
    item->slider_label_count = 0;
}

void ui_menu_add_slider_labeled(UIMenu *menu, const char *text, int32_t action_id,
                                int32_t value, int32_t min_val, int32_t max_val,
                                const char *const *labels, int32_t label_count)
{
    if (menu->item_count >= UI_MAX_MENU_ITEMS)
        return;

    UIMenuItem *item = &menu->items[menu->item_count++];
    item->type = UI_ITEM_SLIDER;
    snprintf(item->text, UI_MAX_TEXT_LEN, "%s", text);
    item->action_id = action_id;
    item->slider_value = value;
    item->slider_min = min_val;
    item->slider_max = max_val;
    item->slider_step = 1;
    item->enabled = true;
    item->hovered = false;
    item->slider_labels = labels;
    item->slider_label_count = label_count;
}

void ui_context_init(UIContext *ctx)
{
    memset(ctx, 0, sizeof(UIContext));
    ctx->fade_alpha = 1.0f;
    ctx->fade_target = 1.0f;
    ctx->fade_speed = 4.0f;
    ctx->visible = true;
}

void ui_context_show(UIContext *ctx)
{
    ctx->visible = true;
    ctx->fade_target = 1.0f;
}

void ui_context_hide(UIContext *ctx)
{
    ctx->fade_target = 0.0f;
}

void ui_context_update(UIContext *ctx, float dt, float mouse_x, float mouse_y, bool mouse_down)
{
    ctx->mouse_x = mouse_x;
    ctx->mouse_y = mouse_y;

    ctx->mouse_clicked = mouse_down && !ctx->mouse_was_down;
    ctx->mouse_was_down = mouse_down;

    if (ctx->fade_alpha < ctx->fade_target)
    {
        ctx->fade_alpha += ctx->fade_speed * dt;
        if (ctx->fade_alpha > ctx->fade_target)
            ctx->fade_alpha = ctx->fade_target;
    }
    else if (ctx->fade_alpha > ctx->fade_target)
    {
        ctx->fade_alpha -= ctx->fade_speed * dt;
        if (ctx->fade_alpha < ctx->fade_target)
            ctx->fade_alpha = ctx->fade_target;
    }

    if (ctx->fade_alpha < 0.01f && ctx->fade_target < 0.01f)
    {
        ctx->visible = false;
    }
}

bool ui_context_is_blocking(const UIContext *ctx)
{
    return ctx->visible;
}

static bool point_in_rect_xywh(float px, float py, float x, float y, float w, float h)
{
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

int32_t ui_menu_update(UIContext *ctx, UIMenu *menu, int32_t window_width, int32_t window_height)
{
    if (!ctx->visible || !menu)
        return 0;

    float w = (float)window_width;
    float h = (float)window_height;
    if (w < 1.0f)
        w = 1.0f;
    if (h < 1.0f)
        h = 1.0f;
    const float min_dim = w < h ? w : h;

    const float item_h_px = clampf_local(min_dim * 0.024f, 12.0f, 20.0f);
    const float button_w_px = clampf_local(w * 0.32f, 200.0f, 480.0f);
    const float button_h_px = clampf_local(item_h_px * 1.8f, 22.0f, 40.0f);
    const float spacing_px = clampf_local(button_h_px * 0.35f, 6.0f, 16.0f);

    const float cx_px = w * 0.5f;
    const float center_y_px = h * 0.55f;
    const float start_y_px = center_y_px - (float)(menu->item_count - 1) * (button_h_px + spacing_px) * 0.5f;
    const float rect_x_px = cx_px - button_w_px * 0.5f;

    int32_t triggered_action = 0;

    for (int32_t i = 0; i < menu->item_count; i++)
    {
        UIMenuItem *item = &menu->items[i];
        if (item->type == UI_ITEM_LABEL || !item->enabled)
        {
            item->hovered = false;
            continue;
        }

        const float rect_y_px = start_y_px + (float)i * (button_h_px + spacing_px);
        item->hovered = point_in_rect_xywh(ctx->mouse_x, ctx->mouse_y, rect_x_px, rect_y_px, button_w_px, button_h_px);

        if (item->hovered && ctx->mouse_clicked)
        {
            triggered_action = item->action_id;

            if (item->type == UI_ITEM_TOGGLE)
            {
                item->toggle_state = !item->toggle_state;
            }

            if (item->type == UI_ITEM_SLIDER)
            {
                float rel_x = (ctx->mouse_x - rect_x_px) / button_w_px;
                if (rel_x < 0.0f)
                    rel_x = 0.0f;
                if (rel_x > 1.0f)
                    rel_x = 1.0f;

                int32_t num_positions = item->slider_max - item->slider_min + 1;
                int32_t new_value = item->slider_min + (int32_t)(rel_x * (float)num_positions);
                if (new_value > item->slider_max)
                    new_value = item->slider_max;
                if (new_value < item->slider_min)
                    new_value = item->slider_min;
                new_value = (new_value / item->slider_step) * item->slider_step;
                item->slider_value = new_value;
            }
        }
    }

    return triggered_action;
}
