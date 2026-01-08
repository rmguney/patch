#ifndef PATCH_CORE_SCENE_H
#define PATCH_CORE_SCENE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Scene Scene;

typedef struct {
    void (*init)(Scene* scene);
    void (*destroy)(Scene* scene);
    void (*update)(Scene* scene, float dt);
    void (*handle_input)(Scene* scene, float mouse_x, float mouse_y, bool left_down, bool right_down);
    void (*render)(Scene* scene, void* renderer);
    const char* (*get_name)(Scene* scene);
} SceneVTable;

struct Scene {
    const SceneVTable* vtable;
    Bounds3D bounds;
    void* user_data;
};

static inline void scene_init(Scene* scene) {
    if (scene && scene->vtable && scene->vtable->init) {
        scene->vtable->init(scene);
    }
}

static inline void scene_destroy(Scene* scene) {
    if (scene && scene->vtable && scene->vtable->destroy) {
        scene->vtable->destroy(scene);
    }
}

static inline void scene_update(Scene* scene, float dt) {
    if (scene && scene->vtable && scene->vtable->update) {
        scene->vtable->update(scene, dt);
    }
}

static inline void scene_handle_input(Scene* scene, float mouse_x, float mouse_y, bool left_down, bool right_down) {
    if (scene && scene->vtable && scene->vtable->handle_input) {
        scene->vtable->handle_input(scene, mouse_x, mouse_y, left_down, right_down);
    }
}

static inline void scene_render(Scene* scene, void* renderer) {
    if (scene && scene->vtable && scene->vtable->render) {
        scene->vtable->render(scene, renderer);
    }
}

static inline const char* scene_get_name(Scene* scene) {
    if (scene && scene->vtable && scene->vtable->get_name) {
        return scene->vtable->get_name(scene);
    }
    return "Unknown";
}

#ifdef __cplusplus
}
#endif

#endif
