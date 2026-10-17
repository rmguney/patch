/* Storage for the profiler state declared in profile.h. Always compiled with
 * PATCH_PROFILE so any target that enables profiling links against it. */
#ifndef PATCH_PROFILE
#define PATCH_PROFILE
#endif

#include "engine/core/profile.h"

ProfileSlot g_profile_slots[PROFILE_COUNT];
ProfileBudget g_profile_budget;
