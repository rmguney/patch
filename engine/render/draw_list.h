#ifndef PATCH_ENGINE_RENDER_DRAW_LIST_H
#define PATCH_ENGINE_RENDER_DRAW_LIST_H

#include "engine/core/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        Vec3 position;
        Vec3 rotation;
        Vec3 scale;
        Vec3 color;
        float alpha;
    } BoxInstance;

#define DRAW_LIST_MAX_BOXES 8192

    typedef struct
    {
        BoxInstance boxes[DRAW_LIST_MAX_BOXES];
        int32_t box_count;
    } DrawList;

    static inline void draw_list_clear(DrawList *list)
    {
        list->box_count = 0;
    }

    static inline void draw_list_add_box(DrawList *list, Vec3 position, Vec3 rotation, Vec3 scale, Vec3 color, float alpha)
    {
        if (list->box_count >= DRAW_LIST_MAX_BOXES)
            return;
        BoxInstance *box = &list->boxes[list->box_count++];
        box->position = position;
        box->rotation = rotation;
        box->scale = scale;
        box->color = color;
        box->alpha = alpha;
    }

#ifdef __cplusplus
}
#endif

#endif
