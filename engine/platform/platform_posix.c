/* clock_gettime and CLOCK_MONOTONIC are POSIX, hidden under strict -std=c23 */
#define _POSIX_C_SOURCE 199309L

#include "platform.h"
#include <time.h>

/* Ticks are CLOCK_MONOTONIC nanoseconds */
#define PLATFORM_TICKS_PER_SECOND 1000000000LL

static int64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * PLATFORM_TICKS_PER_SECOND + (int64_t)ts.tv_nsec;
}

void platform_time_init(void)
{
}

PlatformTime platform_time_now(void)
{
    PlatformTime t;
    t.counter = monotonic_ns();
    return t;
}

float platform_time_delta_seconds(PlatformTime start, PlatformTime end)
{
    return (float)(end.counter - start.counter) / (float)PLATFORM_TICKS_PER_SECOND;
}

int64_t platform_get_ticks(void)
{
    return monotonic_ns();
}

int64_t platform_get_frequency(void)
{
    return PLATFORM_TICKS_PER_SECOND;
}
