#include "platform.h"
#include "engine/core/profile.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static int64_t g_frequency = 0;

#ifdef PATCH_PROFILE
ProfileSlot g_profile_slots[PROFILE_COUNT] = {0};
ProfileBudget g_profile_budget = {0};
#endif

void platform_time_init(void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_frequency = freq.QuadPart;
}

PlatformTime platform_time_now(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    PlatformTime t;
    t.counter = counter.QuadPart;
    return t;
}

float platform_time_delta_seconds(PlatformTime start, PlatformTime end)
{
    if (g_frequency == 0)
        return 0.0f;
    return (float)(end.counter - start.counter) / (float)g_frequency;
}

int64_t platform_get_ticks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

int64_t platform_get_frequency(void)
{
    if (g_frequency == 0)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart;
    }
    return g_frequency;
}
