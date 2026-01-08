#ifndef PATCH_ENGINE_UI_RENDERER_H
#define PATCH_ENGINE_UI_RENDERER_H

#include "../core/ui.h"

namespace patch {

class Renderer;

void ui_render(UIState* ui, Renderer& renderer, int32_t window_width, int32_t window_height);

}

#endif
