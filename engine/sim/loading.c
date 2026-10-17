#include "engine/sim/loading.h"
#include "engine/platform/platform.h"
#include "engine/platform/thread.h"
#include <stdlib.h>

struct BackgroundLoad
{
    const LoadingStage *stages;
    int32_t stage_count;
    LoadingStageFn run;
    void *ctx;

    PlatformThread *thread;
    PlatformMutex *mutex;
    PlatformCond *progressed; /* A stage finished */
    int32_t stages_done;      /* (mutex) */
    bool cancel;              /* (mutex) */

    /* The presenting thread's: its priority before the load, and when its last poll
     * returned (platform ticks) */
    int32_t presenter_priority;
    int64_t last_poll;
};

static void loader_thread(void *arg)
{
    BackgroundLoad *load = (BackgroundLoad *)arg;
    for (int32_t stage = 0; stage < load->stage_count; stage++)
    {
        platform_mutex_lock(load->mutex);
        bool cancel = load->cancel;
        platform_mutex_unlock(load->mutex);
        if (cancel)
            break;

        loading_log_stage(stage, load->stage_count, load->stages[stage].name);
        load->run(load->ctx, stage);

        platform_mutex_lock(load->mutex);
        load->stages_done = stage + 1;
        platform_cond_broadcast(load->progressed);
        platform_mutex_unlock(load->mutex);
    }
}

static void loading_free(BackgroundLoad *load)
{
    if (load->progressed)
        platform_cond_destroy(load->progressed);
    if (load->mutex)
        platform_mutex_destroy(load->mutex);
    free(load);
}

BackgroundLoad *loading_start(const LoadingStage *stages, int32_t stage_count, LoadingStageFn run, void *ctx)
{
    BackgroundLoad *load = (BackgroundLoad *)calloc(1, sizeof(BackgroundLoad));
    if (!load)
        return NULL;
    load->stages = stages;
    load->stage_count = stage_count;
    load->run = run;
    load->ctx = ctx;
    load->mutex = platform_mutex_create();
    load->progressed = platform_cond_create();
    if (load->mutex && load->progressed)
        load->thread = platform_thread_start(loader_thread, load);
    if (!load->thread)
    {
        loading_free(load);
        return NULL;
    }
    load->presenter_priority = platform_thread_raise_priority();
    platform_precise_waits_begin();
    load->last_poll = platform_get_ticks();
    return load;
}

bool loading_poll(BackgroundLoad *load, LoadingState *state, uint32_t interval_ms)
{
    /* Whole milliseconds until the next frame is due, rounded down: a timed wait wakes
     * up to a millisecond late */
    const int64_t frequency = platform_get_frequency();
    const int64_t due = load->last_poll + (int64_t)interval_ms * frequency / 1000;
    const int64_t wait_ms = (due - platform_get_ticks()) * 1000 / frequency;

    platform_mutex_lock(load->mutex);
    if (state->stage_index > load->stages_done && wait_ms > 0)
        platform_cond_wait_ms(load->progressed, load->mutex, (uint32_t)wait_ms);
    const int32_t done = load->stages_done;
    platform_mutex_unlock(load->mutex);
    load->last_poll = platform_get_ticks();

    /* The stage after the last finished one is running */
    const int32_t started = done < load->stage_count ? done + 1 : done;
    while (state->stage_index < started)
        loading_state_advance(state, load->stages[state->stage_index].name);

    float total = 0.0f, finished = 0.0f;
    for (int32_t stage = 0; stage < load->stage_count; stage++)
    {
        total += load->stages[stage].weight;
        if (stage < done)
            finished += load->stages[stage].weight;
    }
    state->progress = total > 0.0f ? finished / total : 1.0f;
    return done == load->stage_count;
}

void loading_finish(BackgroundLoad *load)
{
    if (!load)
        return;
    platform_mutex_lock(load->mutex);
    load->cancel = true;
    platform_mutex_unlock(load->mutex);
    platform_thread_join(load->thread);
    platform_precise_waits_end();
    platform_thread_restore_priority(load->presenter_priority);
    loading_free(load);
}
