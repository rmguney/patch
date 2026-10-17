#include "ui.h"
#include "ui_layout.h"
#include "engine/core/math.h"
#include <string.h>
#include <stdio.h>

/* Menu fade in and out: alpha per second */
#define UI_FADE_SPEED 4.0f

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

void ui_context_hide_now(UIContext *ctx)
{
    ctx->fade_target = 0.0f;
    ctx->fade_alpha = 0.0f;
    ctx->visible = false;
}

void ui_context_update(UIContext *ctx, float dt, float mouse_x, float mouse_y, bool mouse_down)
{
    ctx->mouse_x = mouse_x;
    ctx->mouse_y = mouse_y;

    ctx->mouse_clicked = mouse_down && !ctx->mouse_was_down;
    ctx->mouse_was_down = mouse_down;

    if (ctx->fade_alpha < ctx->fade_target)
    {
        ctx->fade_alpha += UI_FADE_SPEED * dt;
        if (ctx->fade_alpha > ctx->fade_target)
            ctx->fade_alpha = ctx->fade_target;
    }
    else if (ctx->fade_alpha > ctx->fade_target)
    {
        ctx->fade_alpha -= UI_FADE_SPEED * dt;
        if (ctx->fade_alpha < ctx->fade_target)
            ctx->fade_alpha = ctx->fade_target;
    }

    if (ctx->fade_alpha < 0.01f && ctx->fade_target < 0.01f)
    {
        ctx->visible = false;
    }
}

static bool point_in_rect_xywh(float px, float py, float x, float y, float w, float h)
{
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

int32_t ui_menu_update(UIContext *ctx, UIMenu *menu, int32_t window_width, int32_t window_height)
{
    if (!ctx->visible || !menu)
        return 0;

    const UIMenuLayout layout = ui_menu_layout(menu->item_count, window_width, window_height);
    int32_t triggered_action = 0;

    for (int32_t i = 0; i < menu->item_count; i++)
    {
        UIMenuItem *item = &menu->items[i];
        if (item->type == UI_ITEM_LABEL || !item->enabled)
        {
            item->hovered = false;
            continue;
        }

        const float rect_y_px = ui_menu_row_y(&layout, i);
        item->hovered = point_in_rect_xywh(ctx->mouse_x, ctx->mouse_y, layout.x_px, rect_y_px, layout.button_w_px,
                                           layout.button_h_px);

        if (item->hovered && ctx->mouse_clicked)
        {
            triggered_action = item->action_id;

            if (item->type == UI_ITEM_SLIDER)
            {
                float rel_x = (ctx->mouse_x - layout.x_px) / layout.button_w_px;
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
                item->slider_value = new_value;
            }
        }
    }

    return triggered_action;
}
