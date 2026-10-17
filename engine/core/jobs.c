#include "engine/core/jobs.h"
#include "engine/platform/thread.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

/* Chunks per thread when the caller leaves the grain to the job system: enough that a
 * thread finishing early still finds work, few enough that claiming stays negligible */
#define JOB_AUTO_CHUNKS_PER_THREAD 16

/* One running job_parallel_for, on its caller's stack. Workers reach it only through
 * the queue and announce themselves in helpers, both under the mutex: the caller
 * returns, and the job goes away, only once it is off the queue with no helper left. */
typedef struct Job
{
    JobRangeFn fn;
    void *ctx;
    int32_t count;
    int32_t grain;
    int32_t chunk_count;
    atomic_int next_chunk; /* First chunk nobody claimed yet */
    int32_t helpers;       /* Workers inside job_run_chunks for this job (mutex) */
    bool queued;           /* (mutex) */
    struct Job *next;      /* Queue link (mutex) */
} Job;

struct JobSystem
{
    PlatformMutex *mutex;
    PlatformCond *work_ready; /* A job was queued, or the system shuts down */
    PlatformCond *job_left;   /* A job's last helper left it */
    Job *head;                /* Queue of jobs that may have chunks left (mutex) */
    Job *tail;
    bool shutdown; /* (mutex) */
    int32_t worker_count;
    PlatformThread **workers;
};

/* Claims and runs chunks until every chunk is claimed. Claiming needs no ordering: what
 * the chunks write reaches the caller through the mutex the helpers take afterwards. */
static void job_run_chunks(Job *job)
{
    for (;;)
    {
        int32_t chunk = atomic_fetch_add_explicit(&job->next_chunk, 1, memory_order_relaxed);
        if (chunk >= job->chunk_count)
            return;
        int64_t begin = (int64_t)chunk * job->grain;
        int64_t end = begin + job->grain < job->count ? begin + job->grain : job->count;
        job->fn(job->ctx, (int32_t)begin, (int32_t)end);
    }
}

static void job_enqueue(JobSystem *js, Job *job)
{
    job->next = NULL;
    job->queued = true;
    if (js->tail)
        js->tail->next = job;
    else
        js->head = job;
    js->tail = job;
}

/* Takes a job whose chunks are all claimed off the queue, so idle workers skip it */
static void job_dequeue(JobSystem *js, Job *job)
{
    if (!job->queued)
        return;
    Job *prev = NULL;
    for (Job *j = js->head; j != job; j = j->next)
        prev = j;
    if (prev)
        prev->next = job->next;
    else
        js->head = job->next;
    if (js->tail == job)
        js->tail = prev;
    job->queued = false;
}

static void job_worker(void *arg)
{
    JobSystem *js = (JobSystem *)arg;
    platform_mutex_lock(js->mutex);
    for (;;)
    {
        while (!js->head && !js->shutdown)
            platform_cond_wait(js->work_ready, js->mutex);
        if (!js->head)
            break;

        Job *job = js->head;
        job->helpers++;
        platform_mutex_unlock(js->mutex);
        job_run_chunks(job);
        platform_mutex_lock(js->mutex);

        job_dequeue(js, job);
        if (--job->helpers == 0)
            platform_cond_broadcast(js->job_left);
    }
    platform_mutex_unlock(js->mutex);
}

JobSystem *job_system_create(int32_t thread_count)
{
    if (thread_count <= 0)
        thread_count = platform_thread_hardware_count();

    JobSystem *js = (JobSystem *)calloc(1, sizeof(JobSystem));
    if (!js)
        return NULL;
    js->mutex = platform_mutex_create();
    js->work_ready = platform_cond_create();
    js->job_left = platform_cond_create();
    js->workers = (PlatformThread **)calloc((size_t)thread_count, sizeof(PlatformThread *));
    if (!js->mutex || !js->work_ready || !js->job_left || !js->workers)
    {
        job_system_destroy(js);
        return NULL;
    }

    /* A worker the system refuses only means fewer threads */
    for (int32_t i = 0; i < thread_count - 1; i++)
    {
        PlatformThread *worker = platform_thread_start(job_worker, js);
        if (!worker)
            break;
        js->workers[js->worker_count++] = worker;
    }
    return js;
}

void job_system_destroy(JobSystem *js)
{
    if (!js)
        return;
    if (js->worker_count > 0)
    {
        platform_mutex_lock(js->mutex);
        js->shutdown = true;
        platform_cond_broadcast(js->work_ready);
        platform_mutex_unlock(js->mutex);
        for (int32_t i = 0; i < js->worker_count; i++)
            platform_thread_join(js->workers[i]);
    }
    free((void *)js->workers);
    if (js->job_left)
        platform_cond_destroy(js->job_left);
    if (js->work_ready)
        platform_cond_destroy(js->work_ready);
    if (js->mutex)
        platform_mutex_destroy(js->mutex);
    free(js);
}

int32_t job_system_thread_count(const JobSystem *js)
{
    return js ? js->worker_count + 1 : 1;
}

void job_parallel_for(JobSystem *js, int32_t count, int32_t grain, JobRangeFn fn, void *ctx)
{
    if (count <= 0)
        return;
    if (grain <= 0)
    {
        int64_t target_chunks = (int64_t)job_system_thread_count(js) * JOB_AUTO_CHUNKS_PER_THREAD;
        grain = (int32_t)(((int64_t)count + target_chunks - 1) / target_chunks);
    }
    const int32_t chunk_count = (int32_t)(((int64_t)count + grain - 1) / grain);

    /* Nothing to share: run on the caller, in order */
    if (!js || js->worker_count == 0 || chunk_count == 1)
    {
        for (int64_t begin = 0; begin < count; begin += grain)
            fn(ctx, (int32_t)begin, (int32_t)(begin + grain < count ? begin + grain : count));
        return;
    }

    Job job;
    job.fn = fn;
    job.ctx = ctx;
    job.count = count;
    job.grain = grain;
    job.chunk_count = chunk_count;
    atomic_init(&job.next_chunk, 0);
    job.helpers = 0;

    platform_mutex_lock(js->mutex);
    job_enqueue(js, &job);
    platform_cond_broadcast(js->work_ready);
    platform_mutex_unlock(js->mutex);

    job_run_chunks(&job);

    /* Every chunk is claimed; the helpers still running one finish it before leaving */
    platform_mutex_lock(js->mutex);
    job_dequeue(js, &job);
    while (job.helpers > 0)
        platform_cond_wait(js->job_left, js->mutex);
    platform_mutex_unlock(js->mutex);
}
