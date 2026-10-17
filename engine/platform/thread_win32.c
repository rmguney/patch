#include "thread.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#include <stdlib.h>

/* Clock tick precise waits ask for, in ms */
#define PRECISE_WAIT_TICK_MS 1

struct PlatformThread
{
    HANDLE handle;
    PlatformThreadFn fn;
    void *arg;
};

struct PlatformMutex
{
    SRWLOCK lock;
};

struct PlatformCond
{
    CONDITION_VARIABLE cv;
};

int32_t platform_thread_hardware_count(void)
{
    /* Machines with more than 64 logical processors split them into processor groups.
     * A process with affinity to several groups (the default from Windows 11 and Server
     * 2022, which schedule its threads across all of them) can use every active
     * processor of those groups. A process confined to one group (earlier Windows keeps
     * it in its primary group) runs its threads there only, so its affinity mask there
     * is the count: a process started on a subset of the processors (start /affinity)
     * must not oversubscribe them. Moving workers to other groups by hand
     * (SetThreadGroupAffinity) would buy those older systems more threads, at the cost
     * of overriding what the user or the system chose for the process; it is left out. */
    USHORT groups[64];
    USHORT group_count = (USHORT)(sizeof(groups) / sizeof(groups[0]));
    int32_t count = 0;
    if (GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups) && group_count > 1)
    {
        for (USHORT g = 0; g < group_count; g++)
            count += (int32_t)GetActiveProcessorCount(groups[g]);
        return count > 0 ? count : 1;
    }

    DWORD_PTR process_mask = 0, system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
    {
        for (; process_mask != 0; process_mask &= process_mask - 1)
            count++;
    }
    return count > 0 ? count : 1;
}

/* Plain CreateThread: the Universal CRT sets up its per-thread state on first use and
 * frees it when the thread exits, which _beginthreadex once had to arrange */
static DWORD WINAPI thread_entry(LPVOID param)
{
    PlatformThread *thread = (PlatformThread *)param;
    thread->fn(thread->arg);
    return 0;
}

PlatformThread *platform_thread_start(PlatformThreadFn fn, void *arg)
{
    PlatformThread *thread = (PlatformThread *)malloc(sizeof(PlatformThread));
    if (!thread)
        return NULL;
    thread->fn = fn;
    thread->arg = arg;
    thread->handle = CreateThread(NULL, 0, thread_entry, thread, 0, NULL);
    if (!thread->handle)
    {
        free(thread);
        return NULL;
    }
    return thread;
}

void platform_thread_join(PlatformThread *thread)
{
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    free(thread);
}

/* One step above normal is enough to go ahead of every normal priority thread, the
 * process's own workers and other processes' alike; a thread already higher stays */
int32_t platform_thread_raise_priority(void)
{
    HANDLE self = GetCurrentThread();
    const int previous = GetThreadPriority(self);
    if (previous != THREAD_PRIORITY_ERROR_RETURN && previous < THREAD_PRIORITY_ABOVE_NORMAL)
        SetThreadPriority(self, THREAD_PRIORITY_ABOVE_NORMAL);
    return previous;
}

void platform_thread_restore_priority(int32_t priority)
{
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
        SetThreadPriority(GetCurrentThread(), priority);
}

PlatformMutex *platform_mutex_create(void)
{
    PlatformMutex *mutex = (PlatformMutex *)malloc(sizeof(PlatformMutex));
    if (mutex)
        InitializeSRWLock(&mutex->lock);
    return mutex;
}

void platform_mutex_destroy(PlatformMutex *mutex)
{
    /* An SRW lock holds no system resource */
    free(mutex);
}

void platform_mutex_lock(PlatformMutex *mutex)
{
    AcquireSRWLockExclusive(&mutex->lock);
}

void platform_mutex_unlock(PlatformMutex *mutex)
{
    ReleaseSRWLockExclusive(&mutex->lock);
}

PlatformCond *platform_cond_create(void)
{
    PlatformCond *cond = (PlatformCond *)malloc(sizeof(PlatformCond));
    if (cond)
        InitializeConditionVariable(&cond->cv);
    return cond;
}

void platform_cond_destroy(PlatformCond *cond)
{
    free(cond);
}

void platform_cond_wait(PlatformCond *cond, PlatformMutex *mutex)
{
    SleepConditionVariableSRW(&cond->cv, &mutex->lock, INFINITE, 0);
}

bool platform_cond_wait_ms(PlatformCond *cond, PlatformMutex *mutex, uint32_t timeout_ms)
{
    return SleepConditionVariableSRW(&cond->cv, &mutex->lock, (DWORD)timeout_ms, 0) != 0;
}

/* The request counts for this process only (Windows 10 2004 on) */
void platform_precise_waits_begin(void)
{
    timeBeginPeriod(PRECISE_WAIT_TICK_MS);
}

void platform_precise_waits_end(void)
{
    timeEndPeriod(PRECISE_WAIT_TICK_MS);
}

void platform_cond_broadcast(PlatformCond *cond)
{
    WakeAllConditionVariable(&cond->cv);
}
