#include "engine/core/jobs.h"
#include "engine/platform/platform.h"
#include "engine/platform/thread.h"
#include "engine/sim/loading.h"
#include "test_common.h"
#include <stdint.h>
#include <stdlib.h>

/* ---- parallel_for coverage ---- */

typedef struct
{
    int32_t *hits;
    int32_t chunks_bad; /* Chunks outside [0, count) or empty; written only when broken */
    int32_t count;
} CoverCtx;

static void cover_range(void *ctx, int32_t begin, int32_t end)
{
    CoverCtx *c = (CoverCtx *)ctx;
    if (begin < 0 || end > c->count || begin >= end)
    {
        c->chunks_bad = 1;
        return;
    }
    for (int32_t i = begin; i < end; i++)
        c->hits[i]++;
}

static bool covers_once(JobSystem *jobs, int32_t count, int32_t grain)
{
    CoverCtx c = {(int32_t *)calloc((size_t)(count > 0 ? count : 1), sizeof(int32_t)), 0, count};
    if (!c.hits)
        return false;
    job_parallel_for(jobs, count, grain, cover_range, &c);
    bool ok = c.chunks_bad == 0;
    for (int32_t i = 0; i < count && ok; i++)
        ok = c.hits[i] == 1;
    free(c.hits);
    return ok;
}

TEST(jobs_cover_every_index_once)
{
    /* Each index exactly once, for serial, small and full pools, odd counts and grains */
    const int32_t threads[] = {1, 2, 3, 8, 0};
    const int32_t counts[] = {0, 1, 7, 1000, 100003};
    const int32_t grains[] = {0, 1, 3, 64, 1 << 30};
    ASSERT(covers_once(NULL, 1000, 0));
    for (size_t t = 0; t < sizeof(threads) / sizeof(threads[0]); t++)
    {
        JobSystem *jobs = job_system_create(threads[t]);
        ASSERT(jobs != NULL);
        for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++)
            for (size_t g = 0; g < sizeof(grains) / sizeof(grains[0]); g++)
                ASSERT(covers_once(jobs, counts[c], grains[g]));
        job_system_destroy(jobs);
    }
    return 1;
}

TEST(jobs_thread_count)
{
    ASSERT_EQ(job_system_thread_count(NULL), 1);
    JobSystem *one = job_system_create(1);
    JobSystem *three = job_system_create(3);
    JobSystem *all = job_system_create(0);
    ASSERT(one && three && all);
    ASSERT_EQ(job_system_thread_count(one), 1);
    ASSERT_EQ(job_system_thread_count(three), 3);
    ASSERT_EQ(job_system_thread_count(all), platform_thread_hardware_count());
    job_system_destroy(one);
    job_system_destroy(three);
    job_system_destroy(all);
    return 1;
}

/* ---- Nested loops ---- */

#define NEST_ROWS 48
#define NEST_COLS 2000

typedef struct
{
    JobSystem *jobs;
    int32_t *cells; /* NEST_ROWS x NEST_COLS */
} NestCtx;

typedef struct
{
    int32_t *row;
    int32_t value;
} NestRowCtx;

static void nest_fill(void *ctx, int32_t begin, int32_t end)
{
    NestRowCtx *r = (NestRowCtx *)ctx;
    for (int32_t i = begin; i < end; i++)
        r->row[i] += r->value + i;
}

static void nest_rows(void *ctx, int32_t begin, int32_t end)
{
    NestCtx *n = (NestCtx *)ctx;
    for (int32_t row = begin; row < end; row++)
    {
        NestRowCtx r = {n->cells + (size_t)row * NEST_COLS, row * 100000};
        job_parallel_for(n->jobs, NEST_COLS, 0, nest_fill, &r);
    }
}

TEST(jobs_nested_loops_finish)
{
    /* A chunk running its own loop: the inner loop's caller works on it while the other
     * threads may still be busy with the outer one */
    JobSystem *jobs = job_system_create(0);
    ASSERT(jobs != NULL);
    NestCtx n = {jobs, (int32_t *)calloc((size_t)NEST_ROWS * NEST_COLS, sizeof(int32_t))};
    ASSERT(n.cells != NULL);
    job_parallel_for(jobs, NEST_ROWS, 1, nest_rows, &n);
    for (int32_t row = 0; row < NEST_ROWS; row++)
        for (int32_t i = 0; i < NEST_COLS; i++)
            ASSERT_EQ(n.cells[row * NEST_COLS + i], row * 100000 + i);
    free(n.cells);
    job_system_destroy(jobs);
    return 1;
}

/* ---- Lifetime ---- */

static void noop_range(void *ctx, int32_t begin, int32_t end)
{
    (void)ctx;
    (void)begin;
    (void)end;
}

TEST(jobs_create_destroy_repeatedly)
{
    /* Workers that never got work, and workers just back from a loop, both shut down */
    for (int32_t i = 0; i < 40; i++)
    {
        JobSystem *jobs = job_system_create(1 + i % 9);
        ASSERT(jobs != NULL);
        if (i % 2)
            job_parallel_for(jobs, 10000, 1, noop_range, NULL);
        job_system_destroy(jobs);
    }
    return 1;
}

/* ---- Platform primitives ---- */

typedef struct
{
    PlatformMutex *mutex;
    PlatformCond *cond;
    int32_t stage; /* (mutex) */
} Handshake;

static void handshake_thread(void *arg)
{
    Handshake *h = (Handshake *)arg;
    platform_mutex_lock(h->mutex);
    while (h->stage != 1)
        platform_cond_wait(h->cond, h->mutex);
    h->stage = 2;
    platform_cond_broadcast(h->cond);
    platform_mutex_unlock(h->mutex);
}

TEST(platform_cond_wakes_and_times_out)
{
    Handshake h = {platform_mutex_create(), platform_cond_create(), 0};
    ASSERT(h.mutex && h.cond);

    /* Nobody signals: the timed wait gives up after about its timeout. A wake may be
     * spurious, which only starts another wait. */
    platform_mutex_lock(h.mutex);
    bool timed_out = false;
    float waited = 0.0f;
    for (int32_t attempt = 0; attempt < 100 && !timed_out; attempt++)
    {
        PlatformTime t0 = platform_time_now();
        timed_out = !platform_cond_wait_ms(h.cond, h.mutex, 30);
        waited = platform_time_delta_seconds(t0, platform_time_now());
    }
    platform_mutex_unlock(h.mutex);
    ASSERT(timed_out);
    ASSERT(waited > 0.02f);

    /* A thread woken by a broadcast answers through the same condition variable */
    PlatformThread *thread = platform_thread_start(handshake_thread, &h);
    ASSERT(thread != NULL);
    platform_mutex_lock(h.mutex);
    h.stage = 1;
    platform_cond_broadcast(h.cond);
    while (h.stage != 2)
        platform_cond_wait_ms(h.cond, h.mutex, 1000);
    platform_mutex_unlock(h.mutex);
    platform_thread_join(thread);

    platform_cond_destroy(h.cond);
    platform_mutex_destroy(h.mutex);
    return 1;
}

static float ms_since(PlatformTime start)
{
    return platform_time_delta_seconds(start, platform_time_now()) * 1000.0f;
}

TEST(platform_precise_waits_wake_on_time)
{
    /* 16 ms timed waits end within about a millisecond of it; without precise waits
     * Windows wakes them on its 15.6 ms tick, 16 to 31 ms later (about 25 on average) */
    PlatformMutex *mutex = platform_mutex_create();
    PlatformCond *cond = platform_cond_create();
    ASSERT(mutex && cond);
    const int32_t waits = 8;
    platform_precise_waits_begin();
    platform_mutex_lock(mutex);
    PlatformTime t0 = platform_time_now();
    for (int32_t i = 0; i < waits; i++)
        platform_cond_wait_ms(cond, mutex, 16);
    const float mean = ms_since(t0) / (float)waits;
    platform_mutex_unlock(mutex);
    platform_precise_waits_end();
    platform_cond_destroy(cond);
    platform_mutex_destroy(mutex);
    ASSERT_PERF(mean < 18.0f);
    return 1;
}

/* ---- Background load ---- */

#define LOAD_INTERVAL_MS 16
#define LOAD_FRAME_MS 8.0f
#define LOAD_POLLS 12

/* Stage 0 runs until the test releases it */
typedef struct
{
    PlatformMutex *mutex;
    PlatformCond *cond;
    bool released; /* (mutex) */
} HeldStage;

static void held_stage(void *ctx, int32_t stage)
{
    HeldStage *h = (HeldStage *)ctx;
    if (stage != 0)
        return;
    platform_mutex_lock(h->mutex);
    while (!h->released)
        platform_cond_wait(h->cond, h->mutex);
    platform_mutex_unlock(h->mutex);
}

static int compare_float(const void *a, const void *b)
{
    const float x = *(const float *)a;
    const float y = *(const float *)b;
    return (x > y) - (x < y);
}

TEST(loading_polls_keep_their_interval)
{
    /* A caller drawing an 8 ms frame after each poll still gets one every 16 ms: the
     * interval runs from the previous poll, not from the end of the caller's frame
     * (which would make it 24 ms or more) */
    static const LoadingStage stages[2] = {{"Held stage", 1.0f}, {"Last stage", 1.0f}};
    HeldStage held = {platform_mutex_create(), platform_cond_create(), false};
    ASSERT(held.mutex && held.cond);
    BackgroundLoad *load = loading_start(stages, 2, held_stage, &held);
    ASSERT(load != NULL);

    LoadingState state;
    loading_state_init(&state, 2);
    float periods[LOAD_POLLS];
    PlatformTime previous = platform_time_now();
    for (int32_t i = 0; i < LOAD_POLLS; i++)
    {
        const bool done = loading_poll(load, &state, LOAD_INTERVAL_MS);
        const PlatformTime returned = platform_time_now();
        periods[i] = platform_time_delta_seconds(previous, returned) * 1000.0f;
        previous = returned;
        ASSERT(!done);
        while (ms_since(returned) < LOAD_FRAME_MS)
        {
        }
    }
    ASSERT_EQ(state.stage_index, 1);
    ASSERT(state.progress == 0.0f);

    /* Released, the held stage finishes and so does the load */
    platform_mutex_lock(held.mutex);
    held.released = true;
    platform_cond_broadcast(held.cond);
    platform_mutex_unlock(held.mutex);
    while (!loading_poll(load, &state, LOAD_INTERVAL_MS))
    {
    }
    loading_finish(load);
    platform_cond_destroy(held.cond);
    platform_mutex_destroy(held.mutex);
    ASSERT_EQ(state.stage_index, 2);
    ASSERT(state.progress == 1.0f);

    qsort(periods, LOAD_POLLS, sizeof(float), compare_float);
    ASSERT_PERF(periods[LOAD_POLLS / 2] < LOAD_INTERVAL_MS + 3.0f);
    return 1;
}

int main(void)
{
    platform_time_init();
    printf("=== Job System Tests (%d hardware threads) ===\n", platform_thread_hardware_count());

    RUN_TEST(jobs_cover_every_index_once);
    RUN_TEST(jobs_thread_count);
    RUN_TEST(jobs_nested_loops_finish);
    RUN_TEST(jobs_create_destroy_repeatedly);
    RUN_TEST(platform_cond_wakes_and_times_out);
    RUN_TEST(platform_precise_waits_wake_on_time);
    RUN_TEST(loading_polls_keep_their_interval);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
