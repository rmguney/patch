#ifndef PATCH_ENGINE_SIM_UI_LAYOUT_H
#define PATCH_ENGINE_SIM_UI_LAYOUT_H

#include "engine/core/math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Where a menu's rows go in the window: the one layout that the hit tests (ui.c) and the
 * drawing (engine/render/ui_renderer.cpp) share. Pixels, top-left origin. */
typedef struct
{
    float item_h_px; /* text height of a row */
    float button_w_px;
    float button_h_px;
    float spacing_px; /* between rows */
    float x_px;       /* left edge of every row */
    float start_y_px; /* top of row 0 */
} UIMenuLayout;

static inline UIMenuLayout ui_menu_layout(int32_t item_count, int32_t window_width, int32_t window_height)
{
    const float w = window_width > 0 ? (float)window_width : 1.0f;
    const float h = window_height > 0 ? (float)window_height : 1.0f;
    const float min_dim = w < h ? w : h;

    UIMenuLayout layout;
    layout.item_h_px = clampf(min_dim * 0.024f, 12.0f, 20.0f);
    layout.button_w_px = clampf(w * 0.32f, 200.0f, 480.0f);
    layout.button_h_px = clampf(layout.item_h_px * 1.8f, 22.0f, 40.0f);
    layout.spacing_px = clampf(layout.button_h_px * 0.35f, 6.0f, 16.0f);
    layout.x_px = w * 0.5f - layout.button_w_px * 0.5f;
    layout.start_y_px = h * 0.55f - (float)(item_count - 1) * (layout.button_h_px + layout.spacing_px) * 0.5f;
    return layout;
}

/* Top of row `row` */
static inline float ui_menu_row_y(const UIMenuLayout *layout, int32_t row)
{
    return layout->start_y_px + (float)row * (layout->button_h_px + layout->spacing_px);
}

#ifdef __cplusplus
}
#endif

#endif
