#ifndef PATCH_ENGINE_SIM_LOADING_H
#define PATCH_ENGINE_SIM_LOADING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* What the loading screen shows */
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
    }

    /* Logs the start of stage (0-based), on the thread about to run it. A background
     * load's presenting thread writes nothing while stages run: a console or file write
     * can hold it for several frames while the stage's threads keep the processors busy.
     * The log lines go out in one write each: printf to the app's unbuffered stdout
     * takes 17 ms a line on a slow redirect (PowerShell's), the formatted line 0.07 ms. */
    static inline void loading_log_stage(int32_t stage, int32_t stage_count, const char *name)
    {
        char line[128];
        snprintf(line, sizeof(line), "[Loading] (%d/%d) %s...\n", stage + 1, stage_count, name);
        fputs(line, stdout);
        fflush(stdout);
    }

    static inline void loading_state_complete(LoadingState *state)
    {
        state->progress = 1.0f;
        state->stage_name = "Done";
        fputs("[Loading] Complete\n", stdout);
        fflush(stdout);
    }

    /*
     * Background load: the stages of a load run in order on a loader thread while the
     * thread that started it keeps presenting frames, polling for progress in between.
     * Everything the stages build is handed over through the load's mutex: once
     * loading_poll reports a stage done, its results are visible to the poller.
     * The stages may keep every processor busy, so until loading_finish the presenting
     * thread runs ahead of the process's other threads and its timed waits wake on time
     * (platform_thread_raise_priority, platform_precise_waits_begin).
     */
    typedef struct
    {
        const char *name;
        float weight; /* Share of the progress bar: about the stage's share of the time */
    } LoadingStage;

    /* Runs one stage, on the loader thread */
    typedef void (*LoadingStageFn)(void *ctx, int32_t stage);

    typedef struct BackgroundLoad BackgroundLoad;

    /* Starts running run(ctx, 0..stage_count - 1) on a new thread; the stages array must
     * outlive the load. The calling thread is the presenting one: it polls and finishes
     * the load. NULL if no thread could be started (the caller then runs the stages
     * itself). */
    BackgroundLoad *loading_start(const LoadingStage *stages, int32_t stage_count, LoadingStageFn run, void *ctx);

    /* Waits until interval_ms after the previous poll returned (the first poll:
     * interval_ms after the start), or less when the next stage finishes first, then
     * brings state up to date: each stage started since the last poll is announced, and
     * the progress is the weight of the finished stages. The time the caller spends
     * between polls counts towards the interval, so a caller drawing a frame per poll
     * gets one every interval_ms, not every interval_ms plus its frame time, and it
     * never spins. True once every stage has run. */
    bool loading_poll(BackgroundLoad *load, LoadingState *state, uint32_t interval_ms);

    /* Runs no further stage, waits for the one in progress, joins the loader thread,
     * gives the presenting thread its priority back and frees the load: for a finished
     * load, and for one abandoned half way */
    void loading_finish(BackgroundLoad *load);

#ifdef __cplusplus
}
#endif

#endif
