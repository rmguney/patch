#include "ui_renderer.h"
#include "renderer.h"
#include "ui_font.h"

#include <cmath>
#include <cstdio>

namespace patch {

static Vec3 color_primary = {0.22f, 0.62f, 0.78f};
static Vec3 color_primary_bright = {0.34f, 0.82f, 0.92f};
static Vec3 color_secondary = {0.16f, 0.32f, 0.40f};
static Vec3 color_background = {0.05f, 0.07f, 0.10f};
static Vec3 color_panel = {0.07f, 0.12f, 0.16f};
static Vec3 color_text = {0.90f, 0.96f, 0.98f};
static Vec3 color_text_dim = {0.48f, 0.62f, 0.68f};
static Vec3 color_hover = {0.45f, 0.95f, 0.85f};
static Vec3 color_accent = {0.98f, 0.78f, 0.42f};

struct UIDrawContext {
    Renderer* renderer;
    float alpha;
    int32_t window_width;
    int32_t window_height;
};

static void draw_rect(UIDrawContext* ctx, float x, float y, float w, float h, Vec3 color, float alpha) {
    float cx = x + w * 0.5f;
    float cy = y - h * 0.5f;
    ctx->renderer->draw_ui_quad(cx, cy, w, h, color, alpha * ctx->alpha);
}

static void draw_text_centered(UIDrawContext* ctx, float cx, float y, float pixel, Vec3 color, float alpha, const char* text) {
    int32_t len = 0;
    for (const char* p = text; *p; ++p) len++;
    
    float text_width = (float)len * pixel * 6.0f - pixel;
    float x = cx - text_width * 0.5f;
    
    ctx->renderer->draw_ui_text(x, y, pixel, color, alpha * ctx->alpha, text);
}

static void draw_text_left(UIDrawContext* ctx, float x, float y, float pixel, Vec3 color, float alpha, const char* text) {
    ctx->renderer->draw_ui_text(x, y, pixel, color, alpha * ctx->alpha, text);
}

static void draw_button(UIDrawContext* ctx, float cx, float cy, float w, float h, 
                        const char* text, bool hovered, bool enabled) {
    Vec3 bg_color = hovered ? color_secondary : color_panel;
    Vec3 border_color = hovered ? color_hover : color_primary;
    Vec3 text_color = enabled ? (hovered ? color_hover : color_text) : color_text_dim;
    
    float border = 0.004f;
    draw_rect(ctx, cx - w * 0.5f - border, cy + h * 0.5f + border, w + border * 2.0f, h + border * 2.0f, border_color, 0.9f);
    draw_rect(ctx, cx - w * 0.5f, cy + h * 0.5f, w, h, bg_color, 0.95f);
    
    float pixel = 0.007f;
    draw_text_centered(ctx, cx, cy + pixel * 3.5f, pixel, text_color, 1.0f, text);
}

static void draw_title(UIDrawContext* ctx, const char* title) {
    float pixel_large = 0.018f;
    float pixel_small = 0.006f;
    
    draw_text_centered(ctx, 0.0f, 0.65f, pixel_large, color_primary_bright, 1.0f, title);
    
    float line_width = 0.5f;
    float line_y = 0.55f;
    draw_rect(ctx, -line_width * 0.5f, line_y, line_width, 0.003f, color_primary, 0.7f);
}

static void draw_menu(UIDrawContext* ctx, UIMenu* menu) {
    draw_title(ctx, menu->title);
    
    float button_width = 0.4f;
    float button_height = 0.08f;
    float button_spacing = 0.12f;
    float start_y = 0.1f + (float)(menu->item_count - 1) * button_spacing * 0.5f;
    
    for (int32_t i = 0; i < menu->item_count; i++) {
        UIMenuItem* item = &menu->items[i];
        float y = start_y - (float)i * button_spacing;
        
        if (item->type == UI_ITEM_LABEL) {
            if (item->text[0] != '\0') {
                draw_text_centered(ctx, 0.0f, y, 0.005f, color_text_dim, 0.8f, item->text);
            }
        } else if (item->type == UI_ITEM_BUTTON || item->type == UI_ITEM_TOGGLE) {
            char display_text[UI_MAX_TEXT_LEN + 8];
            if (item->type == UI_ITEM_TOGGLE) {
                snprintf(display_text, sizeof(display_text), "%s: %s", 
                         item->text, item->toggle_state ? "ON" : "OFF");
            } else {
                snprintf(display_text, sizeof(display_text), "%s", item->text);
            }
            
            draw_button(ctx, 0.0f, y, button_width, button_height, 
                       display_text, item->hovered, item->enabled);
        } else if (item->type == UI_ITEM_SLIDER) {
            char display_text[UI_MAX_TEXT_LEN + 16];
            snprintf(display_text, sizeof(display_text), "%s: %d", item->text, item->slider_value);
            
            Vec3 bg_color = item->hovered ? color_secondary : color_panel;
            Vec3 border_color = item->hovered ? color_hover : color_primary;
            Vec3 text_color = item->hovered ? color_hover : color_text;
            
            float border = 0.004f;
            draw_rect(ctx, -button_width * 0.5f - border, y + button_height * 0.5f + border, 
                     button_width + border * 2.0f, button_height + border * 2.0f, border_color, 0.9f);
            draw_rect(ctx, -button_width * 0.5f, y + button_height * 0.5f, 
                     button_width, button_height, bg_color, 0.95f);
            
            float fill_ratio = (float)(item->slider_value - item->slider_min) / 
                              (float)(item->slider_max - item->slider_min);
            if (fill_ratio > 0.0f) {
                draw_rect(ctx, -button_width * 0.5f, y + button_height * 0.5f, 
                         button_width * fill_ratio, button_height, color_primary, 0.6f);
            }
            
            float pixel = 0.006f;
            draw_text_centered(ctx, 0.0f, y + pixel * 3.0f, pixel, text_color, 1.0f, display_text);
        }
    }
}

static void draw_overlay(UIDrawContext* ctx) {
    draw_rect(ctx, -1.0f, 1.0f, 2.0f, 2.0f, color_background, 0.85f);
}

static void draw_vignette(UIDrawContext* ctx) {
    float corner_size = 0.3f;
    Vec3 vignette_color = {0.0f, 0.0f, 0.0f};
    
    for (int i = 0; i < 4; i++) {
        float alpha = 0.15f - (float)i * 0.03f;
        float size = corner_size + (float)i * 0.1f;
        
        draw_rect(ctx, -1.0f, 1.0f, size, size, vignette_color, alpha);
        draw_rect(ctx, 1.0f - size, 1.0f, size, size, vignette_color, alpha);
        draw_rect(ctx, -1.0f, -1.0f + size, size, size, vignette_color, alpha);
        draw_rect(ctx, 1.0f - size, -1.0f + size, size, size, vignette_color, alpha);
    }
}

static void draw_decorations(UIDrawContext* ctx) {
    float corner_size = 0.08f;
    float corner_thickness = 0.004f;
    
    Vec3 deco_color = color_primary;
    float deco_alpha = 0.5f;
    
    draw_rect(ctx, -0.95f, 0.95f, corner_size, corner_thickness, deco_color, deco_alpha);
    draw_rect(ctx, -0.95f, 0.95f, corner_thickness, corner_size, deco_color, deco_alpha);
    
    draw_rect(ctx, 0.95f - corner_size, 0.95f, corner_size, corner_thickness, deco_color, deco_alpha);
    draw_rect(ctx, 0.95f - corner_thickness, 0.95f, corner_thickness, corner_size, deco_color, deco_alpha);
    
    draw_rect(ctx, -0.95f, -0.95f + corner_size, corner_size, corner_thickness, deco_color, deco_alpha);
    draw_rect(ctx, -0.95f, -0.95f + corner_size, corner_thickness, corner_size, deco_color, deco_alpha);
    
    draw_rect(ctx, 0.95f - corner_size, -0.95f + corner_size, corner_size, corner_thickness, deco_color, deco_alpha);
    draw_rect(ctx, 0.95f - corner_thickness, -0.95f + corner_size, corner_thickness, corner_size, deco_color, deco_alpha);
}

static void draw_footer(UIDrawContext* ctx) {
    float pixel = 0.004f;
    draw_text_centered(ctx, 0.0f, -0.88f, pixel, color_text_dim, 0.6f, "PATCH PHYSICS SANDBOX");
}

void ui_render(UIState* ui, Renderer& renderer, int32_t window_width, int32_t window_height) {
    if (!ui->visible || ui->fade_alpha < 0.01f) return;
    
    UIDrawContext ctx;
    ctx.renderer = &renderer;
    ctx.alpha = ui->fade_alpha;
    ctx.window_width = window_width;
    ctx.window_height = window_height;
    
    renderer.begin_ui();
    
    draw_overlay(&ctx);
    draw_vignette(&ctx);
    draw_decorations(&ctx);
    
    UIMenu* menu = ui_get_active_menu(ui);
    if (menu) {
        draw_menu(&ctx, menu);
    }
    
    draw_footer(&ctx);
    
    renderer.end_ui();
}

}
