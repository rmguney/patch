#ifndef PATCH_CORE_SCENE_H
#define PATCH_CORE_SCENE_H

#include "engine/core/types.h"
#include "engine/core/rng.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SIM_TIMESTEP (1.0f / 60.0f)
#define SIM_MAX_FRAME_TIME (0.25f)

    typedef struct Scene Scene;

    typedef struct
    {
        void (*init)(Scene *scene);
        void (*destroy)(Scene *scene);
        void (*tick)(Scene *scene);
        void (*handle_input)(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down);
        void (*render)(Scene *scene, void *renderer);
        const char *(*get_name)(Scene *scene);
    } SceneVTable;

    struct Scene
    {
        const SceneVTable *vtable;
        Bounds3D bounds;
        RngState rng;
        float sim_accumulator;
        void *user_data;
    };

    static inline void scene_init(Scene *scene)
    {
        if (scene && scene->vtable && scene->vtable->init)
        {
            scene->vtable->init(scene);
        }
    }

    static inline void scene_destroy(Scene *scene)
    {
        if (scene && scene->vtable && scene->vtable->destroy)
        {
            scene->vtable->destroy(scene);
        }
    }

    static inline void scene_tick(Scene *scene)
    {
        if (scene && scene->vtable && scene->vtable->tick)
        {
            scene->vtable->tick(scene);
        }
    }

    static inline void scene_update(Scene *scene, float dt)
    {
        if (!scene || !scene->vtable || !scene->vtable->tick)
            return;

        if (dt > SIM_MAX_FRAME_TIME)
            dt = SIM_MAX_FRAME_TIME;

        scene->sim_accumulator += dt;

        while (scene->sim_accumulator >= SIM_TIMESTEP)
        {
            scene->vtable->tick(scene);
            scene->sim_accumulator -= SIM_TIMESTEP;
        }
    }

    static inline void scene_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
    {
        if (scene && scene->vtable && scene->vtable->handle_input)
        {
            scene->vtable->handle_input(scene, mouse_x, mouse_y, left_down, right_down);
        }
    }

    static inline void scene_render(Scene *scene, void *renderer)
    {
        if (scene && scene->vtable && scene->vtable->render)
        {
            scene->vtable->render(scene, renderer);
        }
    }

    static inline const char *scene_get_name(Scene *scene)
    {
        if (scene && scene->vtable && scene->vtable->get_name)
        {
            return scene->vtable->get_name(scene);
        }
        return "Unknown";
    }

#ifdef __cplusplus
}
#endif

#endif
