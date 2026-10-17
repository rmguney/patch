/* pthreads, sysconf and clock_gettime are POSIX, hidden under strict -std=c23; the
 * affinity mask (sched_getaffinity, CPU_COUNT) is a GNU extension */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "thread.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sched.h>
#endif

struct PlatformThread
{
    pthread_t handle;
    PlatformThreadFn fn;
    void *arg;
};

struct PlatformMutex
{
    pthread_mutex_t mutex;
};

struct PlatformCond
{
    pthread_cond_t cond;
};

int32_t platform_thread_hardware_count(void)
{
#if defined(__linux__)
    /* The processors this process may run on: taskset, and the cpuset of a container,
     * leave it fewer than the machine has online */
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        int count = CPU_COUNT(&set);
        if (count > 0)
            return (int32_t)count;
    }
#endif
    /* Elsewhere, or past the CPU_SETSIZE processors a cpu_set_t holds: the online count */
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (int32_t)count : 1;
}

static void *thread_entry(void *param)
{
    PlatformThread *thread = (PlatformThread *)param;
    thread->fn(thread->arg);
    return NULL;
}

PlatformThread *platform_thread_start(PlatformThreadFn fn, void *arg)
{
    PlatformThread *thread = (PlatformThread *)malloc(sizeof(PlatformThread));
    if (!thread)
        return NULL;
    thread->fn = fn;
    thread->arg = arg;
    if (pthread_create(&thread->handle, NULL, thread_entry, thread) != 0)
    {
        free(thread);
        return NULL;
    }
    return thread;
}

void platform_thread_join(PlatformThread *thread)
{
    pthread_join(thread->handle, NULL);
    free(thread);
}

/* A thread goes ahead of the default policy's others only with a lower nice value,
 * which takes privileges: the hint does nothing */
int32_t platform_thread_raise_priority(void)
{
    return 0;
}

void platform_thread_restore_priority(int32_t priority)
{
    (void)priority;
}

PlatformMutex *platform_mutex_create(void)
{
    PlatformMutex *mutex = (PlatformMutex *)malloc(sizeof(PlatformMutex));
    if (mutex && pthread_mutex_init(&mutex->mutex, NULL) != 0)
    {
        free(mutex);
        return NULL;
    }
    return mutex;
}

void platform_mutex_destroy(PlatformMutex *mutex)
{
    pthread_mutex_destroy(&mutex->mutex);
    free(mutex);
}

void platform_mutex_lock(PlatformMutex *mutex)
{
    pthread_mutex_lock(&mutex->mutex);
}

void platform_mutex_unlock(PlatformMutex *mutex)
{
    pthread_mutex_unlock(&mutex->mutex);
}

PlatformCond *platform_cond_create(void)
{
    PlatformCond *cond = (PlatformCond *)malloc(sizeof(PlatformCond));
    if (!cond)
        return NULL;
    /* Timed waits run on the monotonic clock, so a wall clock change neither stretches
     * nor cuts them */
    pthread_condattr_t attr;
    bool ok = pthread_condattr_init(&attr) == 0;
    if (ok)
    {
        ok = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0 && pthread_cond_init(&cond->cond, &attr) == 0;
        pthread_condattr_destroy(&attr);
    }
    if (!ok)
    {
        free(cond);
        return NULL;
    }
    return cond;
}

void platform_cond_destroy(PlatformCond *cond)
{
    pthread_cond_destroy(&cond->cond);
    free(cond);
}

void platform_cond_wait(PlatformCond *cond, PlatformMutex *mutex)
{
    pthread_cond_wait(&cond->cond, &mutex->mutex);
}

bool platform_cond_wait_ms(PlatformCond *cond, PlatformMutex *mutex, uint32_t timeout_ms)
{
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&cond->cond, &mutex->mutex, &deadline) == 0;
}

/* Timed waits run on high resolution timers already */
void platform_precise_waits_begin(void)
{
}

void platform_precise_waits_end(void)
{
}

void platform_cond_broadcast(PlatformCond *cond)
{
    pthread_cond_broadcast(&cond->cond);
}
