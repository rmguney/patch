#ifndef PATCH_ENGINE_PLATFORM_H
#define PATCH_ENGINE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int64_t counter;
    } PlatformTime;

    void platform_time_init(void);
    PlatformTime platform_time_now(void);
    float platform_time_delta_seconds(PlatformTime start, PlatformTime end);

    /* Raw tick access for profiling */
    int64_t platform_get_ticks(void);
    int64_t platform_get_frequency(void);

#ifdef __cplusplus
}
#endif

#endif
