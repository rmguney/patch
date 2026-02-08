#ifndef PATCH_SIM_HEARTBEAT_H
#define PATCH_SIM_HEARTBEAT_H

/*
 * HeartbeatScheduler - Rate-limited emission timer.
 * Ported from Veloren's HeartbeatScheduler (particle.rs:4159-4225).
 *
 * Accumulates elapsed time and returns the number of complete intervals
 * since the last query, preserving partial remainders across ticks.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HEARTBEAT_MAX_TIMERS 32

    typedef struct
    {
        float frequency_sec;
        double last_update;
        uint8_t heartbeats;
        bool active;
    } HeartbeatTimer;

    typedef struct
    {
        HeartbeatTimer timers[HEARTBEAT_MAX_TIMERS];
        int32_t timer_count;
        double last_known_time;
    } HeartbeatScheduler;

    static inline void heartbeat_init(HeartbeatScheduler *sched)
    {
        sched->timer_count = 0;
        sched->last_known_time = 0.0;
        for (int32_t i = 0; i < HEARTBEAT_MAX_TIMERS; i++)
            sched->timers[i].active = false;
    }

    static inline void heartbeat_maintain(HeartbeatScheduler *sched, double now)
    {
        sched->last_known_time = now;
        for (int32_t i = 0; i < sched->timer_count; i++)
        {
            HeartbeatTimer *t = &sched->timers[i];
            if (!t->active)
                continue;

            double total = (now - t->last_update) / (double)t->frequency_sec;
            double full = (double)(int64_t)total;
            if (full < 0.0)
                full = 0.0;
            t->heartbeats = (uint8_t)(full > 255.0 ? 255 : (uint8_t)full);

            double partial = total - full;
            t->last_update = now - partial * (double)t->frequency_sec;
        }
    }

    static inline uint8_t heartbeat_get(HeartbeatScheduler *sched, float frequency_sec)
    {
        for (int32_t i = 0; i < sched->timer_count; i++)
        {
            if (sched->timers[i].active && sched->timers[i].frequency_sec == frequency_sec)
                return sched->timers[i].heartbeats;
        }

        if (sched->timer_count < HEARTBEAT_MAX_TIMERS)
        {
            HeartbeatTimer *t = &sched->timers[sched->timer_count++];
            t->frequency_sec = frequency_sec;
            t->last_update = sched->last_known_time;
            t->heartbeats = 0;
            t->active = true;
        }
        return 0;
    }

#ifdef __cplusplus
}
#endif

#endif
