#ifndef PATCH_ENGINE_PLATFORM_THREAD_H
#define PATCH_ENGINE_PLATFORM_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Threads, mutexes and condition variables over Win32 (thread_win32.c) or POSIX
     * threads (thread_posix.c). The objects live on the heap, so this header pulls in no
     * system header. */
    typedef struct PlatformThread PlatformThread;
    typedef struct PlatformMutex PlatformMutex;
    typedef struct PlatformCond PlatformCond;

    typedef void (*PlatformThreadFn)(void *arg);

    /* Logical processors this process may run on, at least 1 */
    int32_t platform_thread_hardware_count(void);

    /* Runs fn(arg) on a new thread; NULL if the system refuses one */
    PlatformThread *platform_thread_start(PlatformThreadFn fn, void *arg);
    /* Waits until the thread's function has returned, then frees the thread */
    void platform_thread_join(PlatformThread *thread);

    /* Puts the calling thread ahead of the process's other threads (one that must keep
     * presenting frames while workers keep every processor busy) and returns the
     * priority to give back with platform_thread_restore_priority. Only a scheduling
     * hint: POSIX leaves the thread as it is, raising one needs privileges there. */
    int32_t platform_thread_raise_priority(void);
    void platform_thread_restore_priority(int32_t priority);

    /* NULL if out of memory */
    PlatformMutex *platform_mutex_create(void);
    void platform_mutex_destroy(PlatformMutex *mutex);
    void platform_mutex_lock(PlatformMutex *mutex);
    void platform_mutex_unlock(PlatformMutex *mutex);

    /* NULL if out of memory */
    PlatformCond *platform_cond_create(void);
    void platform_cond_destroy(PlatformCond *cond);
    /* Unlocks the (locked) mutex while asleep and locks it again before returning. Wakes
     * may be spurious: callers re-check their condition. */
    void platform_cond_wait(PlatformCond *cond, PlatformMutex *mutex);
    /* The same, waking after at most timeout_ms; false when it timed out */
    bool platform_cond_wait_ms(PlatformCond *cond, PlatformMutex *mutex, uint32_t timeout_ms);
    /* Between begin and end, timed waits wake within about a millisecond of their
     * timeout. Windows otherwise wakes a process's timed waits on its 15.6 ms clock tick
     * unless some library in the process asked for finer ticks (a 16 ms wait then takes
     * 16 to 31 ms). Calls nest; POSIX timed waits are this precise already. */
    void platform_precise_waits_begin(void);
    void platform_precise_waits_end(void);
    /* Wakes every thread waiting on cond */
    void platform_cond_broadcast(PlatformCond *cond);

#ifdef __cplusplus
}
#endif

#endif
