#ifndef PATCH_CORE_PROFILE_H
#define PATCH_CORE_PROFILE_H

#include <stdint.h>
#include "engine/platform/platform.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Shared constants (available in both profiling modes) */
#define PROFILE_HISTORY_SIZE 128
#define PROFILE_FRAME_BUDGET_MS 16.667f

/* Compile-time profiling toggle (define PATCH_PROFILE to enable) */
#ifdef PATCH_PROFILE

#define profile_get_ticks() platform_get_ticks()
#define profile_get_frequency() platform_get_frequency()

/* Profiling categories - hierarchical for drill-down analysis */
typedef enum
{
    /* Frame-level */
    PROFILE_FRAME_TOTAL,

    /* Simulation phases (physics/collision reserved for future use) */
    PROFILE_SIM_TICK,
    PROFILE_SIM_PHYSICS,
    PROFILE_SIM_COLLISION,
    PROFILE_SIM_VOXEL_UPDATE,
    PROFILE_SIM_CONNECTIVITY,
    PROFILE_SIM_PARTICLES,

    /* Voxel operations */
    PROFILE_VOXEL_RAYCAST,
    PROFILE_VOXEL_EDIT,
    PROFILE_VOXEL_OCCUPANCY,
    PROFILE_VOXEL_UPLOAD,

    /* Rendering phases */
    PROFILE_RENDER_TOTAL,
    PROFILE_RENDER_SHADOW,
    PROFILE_RENDER_MAIN,
    PROFILE_RENDER_VOXEL,
    PROFILE_RENDER_UI,

    /* Misc */
    PROFILE_VOLUME_INIT,
    PROFILE_PROP_SPAWN,

    PROFILE_COUNT
} ProfileCategory;

static const char *const g_profile_names[PROFILE_COUNT] = {
    "Frame Total",
    "Sim Tick",
    "  Physics",
    "  Collision",
    "  Voxel Update",
    "  Connectivity",
    "  Particles",
    "Voxel Raycast",
    "Voxel Edit",
    "Voxel Occupancy",
    "Voxel Upload",
    "Render Total",
    "  Shadow Pass",
    "  Main Pass",
    "  Voxel Ray",
    "  UI",
    "Volume Init",
    "Prop Spawn"
};

/* Per-category profiling state with rolling history */
typedef struct
{
    int64_t start_tick;
    int64_t total_ticks;
    int64_t max_ticks;
    int64_t min_ticks;
    int32_t sample_count;

    /* Rolling history for percentiles */
    float history_ms[PROFILE_HISTORY_SIZE];
    int32_t history_index;
    int32_t history_count;
} ProfileSlot;

/* Frame budget tracking */
typedef struct
{
    float frame_ms;
    float budget_used_pct;
    int32_t overrun_count;
    int32_t total_frames;
    float worst_frame_ms;
} ProfileBudget;

extern ProfileSlot g_profile_slots[PROFILE_COUNT];
extern ProfileBudget g_profile_budget;

static inline void profile_begin(ProfileCategory cat)
{
    g_profile_slots[cat].start_tick = profile_get_ticks();
}

static inline void profile_end(ProfileCategory cat)
{
    int64_t elapsed = profile_get_ticks() - g_profile_slots[cat].start_tick;
    ProfileSlot *slot = &g_profile_slots[cat];

    slot->total_ticks += elapsed;
    if (elapsed > slot->max_ticks)
        slot->max_ticks = elapsed;
    if (slot->min_ticks == 0 || elapsed < slot->min_ticks)
        slot->min_ticks = elapsed;
    slot->sample_count++;

    /* Record in rolling history */
    int64_t freq = profile_get_frequency();
    float ms = (float)elapsed / (float)freq * 1000.0f;
    slot->history_ms[slot->history_index] = ms;
    slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
    if (slot->history_count < PROFILE_HISTORY_SIZE)
        slot->history_count++;
}

/* Mark end of frame and update budget tracking */
static inline void profile_frame_end(void)
{
    ProfileSlot *frame_slot = &g_profile_slots[PROFILE_FRAME_TOTAL];
    if (frame_slot->history_count > 0)
    {
        int32_t last_idx = (frame_slot->history_index - 1 + PROFILE_HISTORY_SIZE) % PROFILE_HISTORY_SIZE;
        float frame_ms = frame_slot->history_ms[last_idx];

        g_profile_budget.frame_ms = frame_ms;
        g_profile_budget.budget_used_pct = (frame_ms / PROFILE_FRAME_BUDGET_MS) * 100.0f;
        g_profile_budget.total_frames++;

        if (frame_ms > PROFILE_FRAME_BUDGET_MS)
            g_profile_budget.overrun_count++;

        if (frame_ms > g_profile_budget.worst_frame_ms)
            g_profile_budget.worst_frame_ms = frame_ms;
    }
}

static inline float profile_get_avg_ms(ProfileCategory cat)
{
    if (g_profile_slots[cat].sample_count == 0)
        return 0.0f;
    int64_t freq = profile_get_frequency();
    return (float)g_profile_slots[cat].total_ticks /
           (float)g_profile_slots[cat].sample_count / (float)freq * 1000.0f;
}

static inline float profile_get_max_ms(ProfileCategory cat)
{
    int64_t freq = profile_get_frequency();
    return (float)g_profile_slots[cat].max_ticks / (float)freq * 1000.0f;
}

static inline float profile_get_min_ms(ProfileCategory cat)
{
    int64_t freq = profile_get_frequency();
    return (float)g_profile_slots[cat].min_ticks / (float)freq * 1000.0f;
}

/* Simple insertion sort for small arrays (used for percentile calculation) */
static inline void profile_sort_floats(float *arr, int32_t n)
{
    for (int32_t i = 1; i < n; i++)
    {
        float key = arr[i];
        int32_t j = i - 1;
        while (j >= 0 && arr[j] > key)
        {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/* Get percentile (0-100) from rolling history */
static inline float profile_get_percentile_ms(ProfileCategory cat, int32_t percentile)
{
    ProfileSlot *slot = &g_profile_slots[cat];
    if (slot->history_count == 0)
        return 0.0f;

    /* Copy history to temp buffer for sorting */
    float sorted[PROFILE_HISTORY_SIZE];
    for (int32_t i = 0; i < slot->history_count; i++)
        sorted[i] = slot->history_ms[i];

    profile_sort_floats(sorted, slot->history_count);

    int32_t idx = (percentile * slot->history_count) / 100;
    if (idx >= slot->history_count)
        idx = slot->history_count - 1;

    return sorted[idx];
}

static inline float profile_get_p50_ms(ProfileCategory cat) { return profile_get_percentile_ms(cat, 50); }
static inline float profile_get_p95_ms(ProfileCategory cat) { return profile_get_percentile_ms(cat, 95); }
static inline float profile_get_p99_ms(ProfileCategory cat) { return profile_get_percentile_ms(cat, 99); }

static inline void profile_reset(ProfileCategory cat)
{
    ProfileSlot *slot = &g_profile_slots[cat];
    slot->total_ticks = 0;
    slot->max_ticks = 0;
    slot->min_ticks = 0;
    slot->sample_count = 0;
    slot->history_index = 0;
    slot->history_count = 0;
}

static inline void profile_reset_all(void)
{
    for (int i = 0; i < PROFILE_COUNT; i++)
        profile_reset((ProfileCategory)i);

    g_profile_budget.frame_ms = 0.0f;
    g_profile_budget.budget_used_pct = 0.0f;
    g_profile_budget.overrun_count = 0;
    g_profile_budget.total_frames = 0;
    g_profile_budget.worst_frame_ms = 0.0f;
}

static inline const char *profile_get_name(ProfileCategory cat)
{
    if (cat < 0 || cat >= PROFILE_COUNT)
        return "Unknown";
    return g_profile_names[cat];
}

static inline float profile_get_last_ms(ProfileCategory cat)
{
    ProfileSlot *slot = &g_profile_slots[cat];
    if (slot->history_count == 0)
        return 0.0f;
    int32_t last_idx = (slot->history_index - 1 + PROFILE_HISTORY_SIZE) % PROFILE_HISTORY_SIZE;
    return slot->history_ms[last_idx];
}

static inline int32_t profile_get_sample_count(ProfileCategory cat)
{
    return g_profile_slots[cat].sample_count;
}

/* Budget accessors */
static inline float profile_budget_used_pct(void) { return g_profile_budget.budget_used_pct; }
static inline int32_t profile_budget_overruns(void) { return g_profile_budget.overrun_count; }
static inline float profile_budget_worst_ms(void) { return g_profile_budget.worst_frame_ms; }

/* FPS from profiler (single source of truth) */
static inline float profile_get_fps(void)
{
    float ms = profile_get_last_ms(PROFILE_FRAME_TOTAL);
    return ms > 0.001f ? 1000.0f / ms : 0.0f;
}

static inline float profile_get_avg_fps(void)
{
    float ms = profile_get_avg_ms(PROFILE_FRAME_TOTAL);
    return ms > 0.001f ? 1000.0f / ms : 0.0f;
}

#define PROFILE_BEGIN(cat) profile_begin(cat)
#define PROFILE_END(cat) profile_end(cat)
#define PROFILE_FRAME_END() profile_frame_end()

/* Backward compatibility aliases */
#define PROFILE_RAYCAST PROFILE_VOXEL_RAYCAST
#define PROFILE_OCCUPANCY_REBUILD PROFILE_VOXEL_OCCUPANCY
#define PROFILE_CHUNK_UPLOAD PROFILE_VOXEL_UPLOAD
#define PROFILE_DRAW_VOLUME PROFILE_RENDER_VOXEL
#define PROFILE_RAY_RENDER PROFILE_RENDER_VOXEL
#define PROFILE_SHADOW_PASS PROFILE_RENDER_SHADOW
#define PROFILE_MAIN_PASS PROFILE_RENDER_MAIN
#define PROFILE_UI_PASS PROFILE_RENDER_UI

#else /* !PATCH_PROFILE */

/* Stub definitions for non-profiling builds (allows code to compile without PATCH_PROFILE) */
typedef enum
{
    PROFILE_FRAME_TOTAL = 0,
    PROFILE_SIM_TICK,
    PROFILE_SIM_PHYSICS,
    PROFILE_SIM_COLLISION,
    PROFILE_SIM_VOXEL_UPDATE,
    PROFILE_SIM_CONNECTIVITY,
    PROFILE_SIM_PARTICLES,
    PROFILE_VOXEL_RAYCAST,
    PROFILE_VOXEL_EDIT,
    PROFILE_VOXEL_OCCUPANCY,
    PROFILE_VOXEL_UPLOAD,
    PROFILE_RENDER_TOTAL,
    PROFILE_RENDER_SHADOW,
    PROFILE_RENDER_MAIN,
    PROFILE_RENDER_VOXEL,
    PROFILE_RENDER_UI,
    PROFILE_VOLUME_INIT,
    PROFILE_PROP_SPAWN,
    PROFILE_CHUNK_UPLOAD,
    PROFILE_COUNT
} ProfileCategory;

typedef struct
{
    int64_t start_tick;
    int64_t total_ticks;
    int64_t max_ticks;
    int64_t min_ticks;
    int32_t sample_count;
    float history_ms[PROFILE_HISTORY_SIZE];
    int32_t history_index;
    int32_t history_count;
} ProfileSlot;

typedef struct
{
    float frame_ms;
    float budget_used_pct;
    int32_t overrun_count;
    int32_t total_frames;
    float worst_frame_ms;
} ProfileBudget;

extern ProfileSlot g_profile_slots[PROFILE_COUNT];
extern ProfileBudget g_profile_budget;

#define PROFILE_BEGIN(cat) ((void)0)
#define PROFILE_END(cat) ((void)0)
#define PROFILE_FRAME_END() ((void)0)

static inline float profile_get_avg_ms(int cat) { (void)cat; return 0.0f; }
static inline float profile_get_max_ms(int cat) { (void)cat; return 0.0f; }
static inline float profile_get_min_ms(int cat) { (void)cat; return 0.0f; }
static inline float profile_get_last_ms(int cat) { (void)cat; return 0.0f; }
static inline float profile_get_p50_ms(int cat) { (void)cat; return 0.0f; }
static inline float profile_get_p95_ms(int cat) { (void)cat; return 0.0f; }
static inline float profile_get_p99_ms(int cat) { (void)cat; return 0.0f; }
static inline const char *profile_get_name(int cat) { (void)cat; return ""; }
static inline int32_t profile_get_sample_count(int cat) { (void)cat; return 0; }
static inline void profile_reset(int cat) { (void)cat; }
static inline void profile_reset_all(void) {}
static inline float profile_budget_used_pct(void) { return 0.0f; }
static inline int32_t profile_budget_overruns(void) { return 0; }
static inline float profile_budget_worst_ms(void) { return 0.0f; }
static inline float profile_get_fps(void) { return 0.0f; }
static inline float profile_get_avg_fps(void) { return 0.0f; }

/* Backward compatibility aliases */
#define PROFILE_RAYCAST PROFILE_VOXEL_RAYCAST
#define PROFILE_OCCUPANCY_REBUILD PROFILE_VOXEL_OCCUPANCY
#define PROFILE_DRAW_VOLUME PROFILE_RENDER_VOXEL
#define PROFILE_RAY_RENDER PROFILE_RENDER_VOXEL
#define PROFILE_SHADOW_PASS PROFILE_RENDER_SHADOW
#define PROFILE_MAIN_PASS PROFILE_RENDER_MAIN
#define PROFILE_UI_PASS PROFILE_RENDER_UI

#endif /* PATCH_PROFILE */

#ifdef __cplusplus
}
#endif

#endif /* PATCH_CORE_PROFILE_H */
