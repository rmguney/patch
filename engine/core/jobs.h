#ifndef PATCH_ENGINE_CORE_JOBS_H
#define PATCH_ENGINE_CORE_JOBS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Job system: a fixed pool of worker threads that run parallel loops.
     *
     * job_parallel_for cuts an index range into chunks. The calling thread queues the
     * loop, wakes the workers and claims chunks alongside them (an atomic counter hands
     * each chunk to exactly one thread), then sleeps until the workers still busy with a
     * chunk are done. Idle workers sleep on a condition variable: nothing spins.
     */
    typedef struct JobSystem JobSystem;

    /* Runs the indices [begin, end) of a loop */
    typedef void (*JobRangeFn)(void *ctx, int32_t begin, int32_t end);

    /* thread_count threads run the loops, the thread calling job_parallel_for included,
     * so thread_count - 1 workers start (fewer if the system refuses some). 0 or less
     * uses every hardware thread. NULL if out of memory. */
    JobSystem *job_system_create(int32_t thread_count);
    /* Stops and joins the workers; no loop may be running */
    void job_system_destroy(JobSystem *jobs);
    /* Threads running each loop, the caller included; a NULL job system counts as 1 */
    int32_t job_system_thread_count(const JobSystem *jobs);

    /* Calls fn(ctx, begin, end) for chunks of at most grain indices covering [0, count),
     * each exactly once, and returns when all are done. grain 0 or less picks chunks for
     * about 16 per thread, so threads that finish early find more. Chunks run at once on
     * different threads and in any order: fn may write only what its own indices own,
     * and what it computes must not depend on how the range is cut. A NULL job system
     * runs the chunks in order on the caller. fn may itself call job_parallel_for. */
    void job_parallel_for(JobSystem *jobs, int32_t count, int32_t grain, JobRangeFn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
