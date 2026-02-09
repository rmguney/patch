#ifndef PATCH_GAME_DAY_NIGHT_H
#define PATCH_GAME_DAY_NIGHT_H

#include "content/scenes.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float time_of_day;    /* 0.0-1.0 (0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset) */
        float cycle_duration; /* Real seconds per full cycle */
    } DayNightCycle;

    DayNightCycle day_night_create(float cycle_duration_seconds);
    void day_night_update(DayNightCycle *cycle, float dt);
    void day_night_apply_lighting(const DayNightCycle *cycle, SceneLighting *lighting);

#ifdef __cplusplus
}
#endif

#endif
