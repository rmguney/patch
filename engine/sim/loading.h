#ifndef PATCH_ENGINE_SIM_LOADING_H
#define PATCH_ENGINE_SIM_LOADING_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define LOADING_MAX_STAGE_NAME 64

    typedef struct
    {
        const char *stage_name;
        float progress;
        int32_t stage_index;
        int32_t stage_count;
    } LoadingState;

    static inline void loading_state_init(LoadingState *state, int32_t stage_count)
    {
        state->stage_name = "";
        state->progress = 0.0f;
        state->stage_index = 0;
        state->stage_count = stage_count;
    }

    static inline void loading_state_advance(LoadingState *state, const char *name)
    {
        state->stage_index++;
        state->stage_name = name;
        state->progress = (float)state->stage_index / (float)state->stage_count;
        printf("[Loading] (%d/%d) %s...\n", state->stage_index, state->stage_count, name);
        fflush(stdout);
    }

    static inline void loading_state_complete(LoadingState *state)
    {
        state->progress = 1.0f;
        state->stage_name = "Done";
        printf("[Loading] Complete\n");
        fflush(stdout);
    }

#ifdef __cplusplus
}
#endif

#endif
