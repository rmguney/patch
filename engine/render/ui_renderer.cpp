#include "ui_renderer.h"
#include "renderer.h"
#include "ui_font.h"
#include <cstdio>

namespace patch
{

    static Vec3 color_primary = {0.22f, 0.62f, 0.78f};
    static Vec3 color_primary_bright = {0.34f, 0.82f, 0.92f};
    static Vec3 color_secondary = {0.16f, 0.32f, 0.40f};
    static Vec3 color_background = {0.05f, 0.07f, 0.10f};
    static Vec3 color_panel = {0.07f, 0.12f, 0.16f};
    static Vec3 color_text = {0.90f, 0.96f, 0.98f};
    static Vec3 color_text_dim = {0.48f, 0.62f, 0.68f};
    static Vec3 color_hover = {0.45f, 0.95f, 0.85f};

    struct UIDrawContext
    {
        Renderer *renderer;
        float alpha;
        int32_t window_width;
        int32_t window_height;
    };

    static inline float clampf_local(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static void draw_rect_px(UIDrawContext *ctx, float x_px, float y_px, float w_px, float h_px, Vec3 color, float alpha)
    {
        ctx->renderer->draw_ui_quad_px(x_px, y_px, w_px, h_px, color, alpha * ctx->alpha);
    }

    static void draw_text_centered_px(UIDrawContext *ctx, float cx_px, float y_px, float text_h_px, Vec3 color, float alpha, const char *text)
    {
        if (!text)
            return;
        const float text_w_px = Renderer::ui_text_width_px(text, text_h_px);
        const float x_px = cx_px - text_w_px * 0.5f;
        ctx->renderer->draw_ui_text_px(x_px, y_px, text_h_px, color, alpha * ctx->alpha, text);
    }

    static void draw_button_px(UIDrawContext *ctx, float cx_px, float cy_px, float w_px, float h_px,
                               float text_h_px, const char *text, bool hovered, bool enabled)
    {
        Vec3 bg_color = hovered ? color_secondary : color_panel;
        Vec3 border_color = hovered ? color_hover : color_primary;
        Vec3 text_color = enabled ? (hovered ? color_hover : color_text) : color_text_dim;

        const float unit = text_h_px / 7.0f;
        const float border_px = clampf_local(unit * 0.7f, 1.0f, 4.0f);

        const float x_px = cx_px - w_px * 0.5f;
        const float y_px = cy_px - h_px * 0.5f;
        draw_rect_px(ctx, x_px - border_px, y_px - border_px, w_px + border_px * 2.0f, h_px + border_px * 2.0f, border_color, 0.9f);
        draw_rect_px(ctx, x_px, y_px, w_px, h_px, bg_color, 0.95f);

        const float text_y_px = y_px + (h_px - text_h_px) * 0.5f;
        draw_text_centered_px(ctx, cx_px, text_y_px, text_h_px, text_color, 1.0f, text);
    }

    static void draw_title_px(UIDrawContext *ctx, const char *title, float title_h_px)
    {
        const float w = (float)((ctx->window_width > 0) ? ctx->window_width : 1);
        const float h = (float)((ctx->window_height > 0) ? ctx->window_height : 1);
        const float min_dim = w < h ? w : h;

        const float cx_px = w * 0.5f;
        const float y_px = h * 0.175f;
        draw_text_centered_px(ctx, cx_px, y_px, title_h_px, color_primary_bright, 1.0f, title);

        const float line_w_px = w * 0.25f;
        const float line_h_px = clampf_local(min_dim * 0.003f, 2.0f, 6.0f);
        const float line_x_px = cx_px - line_w_px * 0.5f;
        const float line_y_px = y_px + title_h_px + (title_h_px / 7.0f) * 2.0f;
        draw_rect_px(ctx, line_x_px, line_y_px, line_w_px, line_h_px, color_primary, 0.7f);
    }

    static void draw_menu_px(UIDrawContext *ctx, UIMenu *menu)
    {
        const float w = (float)((ctx->window_width > 0) ? ctx->window_width : 1);
        const float h = (float)((ctx->window_height > 0) ? ctx->window_height : 1);
        const float min_dim = w < h ? w : h;

        const float title_h_px = clampf_local(min_dim * 0.06f, 28.0f, 56.0f);
        const float item_h_px = clampf_local(min_dim * 0.024f, 12.0f, 20.0f);
        const float label_h_px = clampf_local(item_h_px * 0.85f, 10.0f, 18.0f);
        const float unit = item_h_px / 7.0f;

        draw_title_px(ctx, menu->title, title_h_px);

        const float button_w_px = clampf_local(w * 0.32f, 200.0f, 480.0f);
        const float button_h_px = clampf_local(item_h_px * 1.8f, 22.0f, 40.0f);
        const float spacing_px = clampf_local(button_h_px * 0.35f, 6.0f, 16.0f);

        const float cx_px = w * 0.5f;
        const float center_y_px = h * 0.55f;
        const float start_y_px = center_y_px - (float)(menu->item_count - 1) * (button_h_px + spacing_px) * 0.5f;

        for (int32_t i = 0; i < menu->item_count; i++)
        {
            UIMenuItem *item = &menu->items[i];
            float row_y_px = start_y_px + (float)i * (button_h_px + spacing_px);

            if (item->type == UI_ITEM_LABEL)
            {
                if (item->text[0] != '\0')
                {
                    draw_text_centered_px(ctx, cx_px, row_y_px, label_h_px, color_text_dim, 0.8f, item->text);
                }
            }
            else if (item->type == UI_ITEM_BUTTON || item->type == UI_ITEM_TOGGLE)
            {
                char display_text[UI_MAX_TEXT_LEN + 8];
                if (item->type == UI_ITEM_TOGGLE)
                {
                    snprintf(display_text, sizeof(display_text), "%s: %s",
                             item->text, item->toggle_state ? "ON" : "OFF");
                }
                else
                {
                    snprintf(display_text, sizeof(display_text), "%s", item->text);
                }

                draw_button_px(ctx, cx_px, row_y_px + button_h_px * 0.5f, button_w_px, button_h_px,
                               item_h_px, display_text, item->hovered, item->enabled);
            }
            else if (item->type == UI_ITEM_SLIDER)
            {
                char display_text[UI_MAX_TEXT_LEN + 16];
                if (item->slider_labels && item->slider_label_count > 0 &&
                    item->slider_value >= 0 && item->slider_value < item->slider_label_count)
                {
                    snprintf(display_text, sizeof(display_text), "%s: %s",
                             item->text, item->slider_labels[item->slider_value]);
                }
                else
                {
                    snprintf(display_text, sizeof(display_text), "%s: %d", item->text, item->slider_value);
                }

                bool can_interact = item->enabled && item->hovered;
                Vec3 bg_color = can_interact ? color_secondary : color_panel;
                Vec3 border_color = can_interact ? color_hover : (item->enabled ? color_primary : color_text_dim);
                Vec3 text_color = item->enabled ? (can_interact ? color_hover : color_text) : color_text_dim;

                const float border_px = clampf_local(unit * 0.7f, 1.0f, 4.0f);
                const float x_px = cx_px - button_w_px * 0.5f;
                const float y_px = row_y_px;

                draw_rect_px(ctx, x_px - border_px, y_px - border_px, button_w_px + border_px * 2.0f, button_h_px + border_px * 2.0f, border_color, 0.9f);
                draw_rect_px(ctx, x_px, y_px, button_w_px, button_h_px, bg_color, 0.95f);

                float fill_ratio = (float)(item->slider_value - item->slider_min) /
                                   (float)(item->slider_max - item->slider_min);
                if (fill_ratio < 0.0f)
                    fill_ratio = 0.0f;
                if (fill_ratio > 1.0f)
                    fill_ratio = 1.0f;
                if (fill_ratio > 0.0f)
                {
                    draw_rect_px(ctx, x_px, y_px, button_w_px * fill_ratio, button_h_px, color_primary, 0.6f);
                }

                const float text_y_px = y_px + (button_h_px - item_h_px) * 0.5f;
                draw_text_centered_px(ctx, cx_px, text_y_px, item_h_px, text_color, 1.0f, display_text);
            }
        }
    }

    static void draw_overlay_px(UIDrawContext *ctx)
    {
        const float w = (float)((ctx->window_width > 0) ? ctx->window_width : 1);
        const float h = (float)((ctx->window_height > 0) ? ctx->window_height : 1);
        draw_rect_px(ctx, 0.0f, 0.0f, w, h, color_background, 0.85f);
    }

    static void draw_vignette_px(UIDrawContext *ctx)
    {
        const float w = (float)((ctx->window_width > 0) ? ctx->window_width : 1);
        const float h = (float)((ctx->window_height > 0) ? ctx->window_height : 1);
        const float min_dim = w < h ? w : h;

        const float corner_base_px = min_dim * 0.15f;
        const float corner_step_px = min_dim * 0.05f;
        Vec3 vignette_color = {0.0f, 0.0f, 0.0f};

        for (int i = 0; i < 4; i++)
        {
            float alpha = 0.15f - (float)i * 0.03f;
            float size = corner_base_px + (float)i * corner_step_px;

            draw_rect_px(ctx, 0.0f, 0.0f, size, size, vignette_color, alpha);
            draw_rect_px(ctx, w - size, 0.0f, size, size, vignette_color, alpha);
            draw_rect_px(ctx, 0.0f, h - size, size, size, vignette_color, alpha);
            draw_rect_px(ctx, w - size, h - size, size, size, vignette_color, alpha);
        }
    }

    static void draw_decorations_px(UIDrawContext *ctx)
    {
        const float w = (float)((ctx->window_width > 0) ? ctx->window_width : 1);
        const float h = (float)((ctx->window_height > 0) ? ctx->window_height : 1);
        const float min_dim = w < h ? w : h;

        const float margin_px = min_dim * 0.025f;
        const float corner_size_px = min_dim * 0.04f;
        const float corner_thickness_px = clampf_local(min_dim * 0.002f, 2.0f, 6.0f);

        Vec3 deco_color = color_primary;
        float deco_alpha = 0.5f;

        draw_rect_px(ctx, margin_px, margin_px, corner_size_px, corner_thickness_px, deco_color, deco_alpha);
        draw_rect_px(ctx, margin_px, margin_px, corner_thickness_px, corner_size_px, deco_color, deco_alpha);

        draw_rect_px(ctx, w - margin_px - corner_size_px, margin_px, corner_size_px, corner_thickness_px, deco_color, deco_alpha);
        draw_rect_px(ctx, w - margin_px - corner_thickness_px, margin_px, corner_thickness_px, corner_size_px, deco_color, deco_alpha);

        draw_rect_px(ctx, margin_px, h - margin_px - corner_thickness_px, corner_size_px, corner_thickness_px, deco_color, deco_alpha);
        draw_rect_px(ctx, margin_px, h - margin_px - corner_size_px, corner_thickness_px, corner_size_px, deco_color, deco_alpha);

        draw_rect_px(ctx, w - margin_px - corner_size_px, h - margin_px - corner_thickness_px, corner_size_px, corner_thickness_px, deco_color, deco_alpha);
        draw_rect_px(ctx, w - margin_px - corner_thickness_px, h - margin_px - corner_size_px, corner_thickness_px, corner_size_px, deco_color, deco_alpha);
    }

    static void draw_footer_px(UIDrawContext *ctx)
    {
        const float w = (float)((ctx->window_width > 0) ? ctx->window_width : 1);
        const float h = (float)((ctx->window_height > 0) ? ctx->window_height : 1);
        const float min_dim = w < h ? w : h;
        const float text_h_px = clampf_local(min_dim * 0.025f, 12.0f, 20.0f);
        const float unit = text_h_px / 7.0f;
        draw_text_centered_px(ctx, w * 0.5f, h - (unit * 10.0f), text_h_px, color_text_dim, 0.6f, "PATCH PHYSICS SANDBOX");
    }

    void ui_render(const UIContext *ui_ctx, UIMenu *menu, Renderer &renderer,
                   int32_t window_width, int32_t window_height)
    {
        if (!ui_ctx || !ui_ctx->visible || ui_ctx->fade_alpha < 0.01f)
            return;

        UIDrawContext ctx;
        ctx.renderer = &renderer;
        ctx.alpha = ui_ctx->fade_alpha;
        ctx.window_width = window_width;
        ctx.window_height = window_height;

        renderer.begin_ui();

        draw_overlay_px(&ctx);
        draw_vignette_px(&ctx);
        draw_decorations_px(&ctx);

        if (menu)
        {
            draw_menu_px(&ctx, menu);
        }

        draw_footer_px(&ctx);

        renderer.end_ui();
    }

}
