#ifndef PATCH_CORE_PROFILE_H
#define PATCH_CORE_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include "engine/core/types.h"
#include "engine/platform/platform.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PROFILE_HISTORY_SIZE 128
#define PROFILE_FRAME_BUDGET_MS 16.667f

    /* Profiling categories - hierarchical for drill-down analysis. Shared by both
     * modes so code naming a category compiles with and without PATCH_PROFILE. */
    typedef enum
    {
        /* Frame-level */
        PROFILE_FRAME_TOTAL,
        PROFILE_POLL_EVENTS,
        PROFILE_UI_UPDATE,

        /* Simulation phases */
        PROFILE_SIM_TICK,
        PROFILE_SIM_PHYSICS,
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
        PROFILE_RENDER_UI_OVERLAY,
        PROFILE_RENDER_UI,

        /* Misc */
        PROFILE_VOLUME_INIT,

        /* Detailed render breakdown */
        PROFILE_RENDER_GBUFFER,
        PROFILE_RENDER_OBJECTS,
        PROFILE_RENDER_LIGHTING,
        PROFILE_RENDER_AO,
        PROFILE_RENDER_DENOISE,

        /* Sub-profiling for render_main (spike investigation) */
        PROFILE_RENDER_VOLUME_BEGIN,
        PROFILE_RENDER_CHUNK_UPLOAD,
        PROFILE_RENDER_SHADOW_VOLUME,

        /* Sub-profiling for shadow volume (spike investigation) */
        PROFILE_SHADOW_TERRAIN_PACK,
        PROFILE_SHADOW_MIP_REGEN,
        PROFILE_SHADOW_UPLOAD,

        PROFILE_COUNT
    } ProfileCategory;

    /* Categories timed around operations of very different sizes, several times a frame:
     * object recalcs, splits and destruction; terrain and object raycasts; edit batches
     * (digs, detached islands, snow). Their scopes add up over the frame and
     * PROFILE_FRAME_END records the sum as one sample, so their stats are cost per frame
     * instead of an average over unlike calls. */
#define PROFILE_PER_FRAME_CATEGORIES \
    ((1u << PROFILE_SIM_VOXEL_UPDATE) | (1u << PROFILE_VOXEL_RAYCAST) | (1u << PROFILE_VOXEL_EDIT))
    STATIC_ASSERT(PROFILE_COUNT <= 32, "PROFILE_PER_FRAME_CATEGORIES is a 32-bit mask");

    /* Per-category profiling state with rolling history */
    typedef struct
    {
        int64_t start_tick;
        int64_t total_ticks;
        int64_t max_ticks;
        int64_t min_ticks;
        int64_t frame_ticks; /* PROFILE_PER_FRAME_CATEGORIES: this frame's scopes so far */
        int32_t sample_count;

        /* Rolling history for percentiles */
        int32_t history_index;
        int32_t history_count;
        float history_ms[PROFILE_HISTORY_SIZE];

        bool frame_pending; /* frame_ticks holds scopes not yet recorded */
    } ProfileSlot;

    /* Frame budget tracking */
    typedef struct
    {
        float budget_used_pct;
        int32_t overrun_count;
        float worst_frame_ms;
    } ProfileBudget;

    /* Defined in profile.c, which is always built with PATCH_PROFILE */
    extern ProfileSlot g_profile_slots[PROFILE_COUNT];
    extern ProfileBudget g_profile_budget;

/* Compile-time profiling toggle (define PATCH_PROFILE to enable) */
#ifdef PATCH_PROFILE

    static const char *const g_profile_names[PROFILE_COUNT] = {
        "Frame Total",
        "Poll Events",
        "UI Update",
        "Scene Tick",
        "  Physics",
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
        "  UI Overlay",
        "  UI",
        "Volume Init",
        "  G-buffer",
        "  Objects",
        "  Lighting",
        "  AO",
        "  Denoise",
        "    Volume Begin",
        "    Chunk Upload",
        "    Shadow Volume",
        "      Terrain Pack",
        "      Mip Regen",
        "      Upload"};

    static inline void profile_begin(ProfileCategory cat)
    {
        g_profile_slots[cat].start_tick = platform_get_ticks();
    }

    /* Adds a sample of `elapsed` ticks to the category (profile_end measures one) */
    static inline void profile_record_ticks(ProfileCategory cat, int64_t elapsed)
    {
        ProfileSlot *slot = &g_profile_slots[cat];

        slot->total_ticks += elapsed;
        if (elapsed > slot->max_ticks)
            slot->max_ticks = elapsed;
        /* sample_count, not min_ticks == 0, marks "no sample yet": a zero-tick
         * sample is a real minimum */
        if (slot->sample_count == 0 || elapsed < slot->min_ticks)
            slot->min_ticks = elapsed;
        slot->sample_count++;

        /* Record in rolling history */
        int64_t freq = platform_get_frequency();
        float ms = (float)elapsed / (float)freq * 1000.0f;
        slot->history_ms[slot->history_index] = ms;
        slot->history_index = (slot->history_index + 1) % PROFILE_HISTORY_SIZE;
        if (slot->history_count < PROFILE_HISTORY_SIZE)
            slot->history_count++;
    }

    static inline void profile_end(ProfileCategory cat)
    {
        ProfileSlot *slot = &g_profile_slots[cat];
        int64_t elapsed = platform_get_ticks() - slot->start_tick;
        if (PROFILE_PER_FRAME_CATEGORIES & (1u << cat))
        {
            slot->frame_ticks += elapsed;
            slot->frame_pending = true;
        }
        else
        {
            profile_record_ticks(cat, elapsed);
        }
    }

    /* Mark end of frame: record the per-frame categories and update budget tracking */
    static inline void profile_frame_end(void)
    {
        for (int i = 0; i < PROFILE_COUNT; i++)
        {
            ProfileSlot *slot = &g_profile_slots[i];
            if (!slot->frame_pending)
                continue;
            profile_record_ticks((ProfileCategory)i, slot->frame_ticks);
            slot->frame_ticks = 0;
            slot->frame_pending = false;
        }

        ProfileSlot *frame_slot = &g_profile_slots[PROFILE_FRAME_TOTAL];
        if (frame_slot->history_count > 0)
        {
            int32_t last_idx = (frame_slot->history_index - 1 + PROFILE_HISTORY_SIZE) % PROFILE_HISTORY_SIZE;
            float frame_ms = frame_slot->history_ms[last_idx];

            g_profile_budget.budget_used_pct = (frame_ms / PROFILE_FRAME_BUDGET_MS) * 100.0f;

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
        int64_t freq = platform_get_frequency();
        return (float)g_profile_slots[cat].total_ticks /
               (float)g_profile_slots[cat].sample_count / (float)freq * 1000.0f;
    }

    static inline float profile_get_max_ms(ProfileCategory cat)
    {
        int64_t freq = platform_get_frequency();
        return (float)g_profile_slots[cat].max_ticks / (float)freq * 1000.0f;
    }

    static inline float profile_get_min_ms(ProfileCategory cat)
    {
        int64_t freq = platform_get_frequency();
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
        const ProfileSlot *slot = &g_profile_slots[cat];
        int32_t count = slot->history_count;
        if (count <= 0 || count > PROFILE_HISTORY_SIZE)
            return 0.0f;

        /* Copy history to temp buffer for sorting */
        float sorted[PROFILE_HISTORY_SIZE];
        for (int32_t i = 0; i < count; i++)
            sorted[i] = slot->history_ms[i];

        profile_sort_floats(sorted, count);

        int32_t idx = (percentile * count) / 100;
        if (idx >= count)
            idx = count - 1;
        if (idx < 0)
            idx = 0;

        return sorted[idx];
    }

    static inline float profile_get_p50_ms(ProfileCategory cat) { return profile_get_percentile_ms(cat, 50); }
    static inline float profile_get_p95_ms(ProfileCategory cat) { return profile_get_percentile_ms(cat, 95); }
    static inline float profile_get_p99_ms(ProfileCategory cat) { return profile_get_percentile_ms(cat, 99); }

    /* Trend detection: compare first third vs last third of history buffer
     * Returns ratio of last_third_avg / first_third_avg
     * >1.5 indicates degradation over time */
    static inline float profile_get_trend_ratio(ProfileCategory cat)
    {
        ProfileSlot *slot = &g_profile_slots[cat];
        if (slot->history_count < 6)
            return 1.0f;

        int32_t third = slot->history_count / 3;

        float first_sum = 0.0f;
        float last_sum = 0.0f;

        /* Calculate oldest third average (first entries in ring buffer) */
        int32_t start_idx = (slot->history_index - slot->history_count + PROFILE_HISTORY_SIZE) % PROFILE_HISTORY_SIZE;
        for (int32_t i = 0; i < third; i++)
        {
            int32_t idx = (start_idx + i) % PROFILE_HISTORY_SIZE;
            first_sum += slot->history_ms[idx];
        }

        /* Calculate newest third average (last entries in ring buffer) */
        for (int32_t i = 0; i < third; i++)
        {
            int32_t idx = (slot->history_index - third + i + PROFILE_HISTORY_SIZE) % PROFILE_HISTORY_SIZE;
            last_sum += slot->history_ms[idx];
        }

        float first_avg = first_sum / (float)third;
        float last_avg = last_sum / (float)third;

        if (first_avg < 0.001f)
            return 1.0f;

        return last_avg / first_avg;
    }

    static inline void profile_reset(ProfileCategory cat)
    {
        ProfileSlot *slot = &g_profile_slots[cat];
        slot->total_ticks = 0;
        slot->max_ticks = 0;
        slot->min_ticks = 0;
        slot->sample_count = 0;
        slot->frame_ticks = 0;
        slot->frame_pending = false;
        slot->history_index = 0;
        slot->history_count = 0;
    }

    static inline void profile_reset_all(void)
    {
        for (int i = 0; i < PROFILE_COUNT; i++)
            profile_reset((ProfileCategory)i);

        g_profile_budget.budget_used_pct = 0.0f;
        g_profile_budget.overrun_count = 0;
        g_profile_budget.worst_frame_ms = 0.0f;
    }

    static inline const char *profile_get_name(ProfileCategory cat)
    {
        if ((unsigned)cat >= (unsigned)PROFILE_COUNT)
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

    static inline float profile_get_avg_fps(void)
    {
        float ms = profile_get_avg_ms(PROFILE_FRAME_TOTAL);
        return ms > 0.001f ? 1000.0f / ms : 0.0f;
    }

#define PROFILE_BEGIN(cat) profile_begin(cat)
#define PROFILE_END(cat) profile_end(cat)
#define PROFILE_FRAME_END() profile_frame_end()

#else /* !PATCH_PROFILE */

/* Profiling compiled out: scopes vanish and every query reads zero */
#define PROFILE_BEGIN(cat) ((void)0)
#define PROFILE_END(cat) ((void)0)
#define PROFILE_FRAME_END() ((void)0)

    static inline float profile_get_avg_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_max_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_min_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_last_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_p50_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_p95_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_p99_ms(ProfileCategory cat) { (void)cat; return 0.0f; }
    static inline float profile_get_trend_ratio(ProfileCategory cat) { (void)cat; return 1.0f; }
    static inline const char *profile_get_name(ProfileCategory cat) { (void)cat; return ""; }
    static inline int32_t profile_get_sample_count(ProfileCategory cat) { (void)cat; return 0; }
    static inline void profile_record_ticks(ProfileCategory cat, int64_t elapsed) { (void)cat; (void)elapsed; }
    static inline void profile_reset(ProfileCategory cat) { (void)cat; }
    static inline void profile_reset_all(void) {}
    static inline float profile_budget_used_pct(void) { return 0.0f; }
    static inline int32_t profile_budget_overruns(void) { return 0; }
    static inline float profile_budget_worst_ms(void) { return 0.0f; }
    static inline float profile_get_avg_fps(void) { return 0.0f; }

#endif /* PATCH_PROFILE */

#ifdef __cplusplus
}
#endif

#endif /* PATCH_CORE_PROFILE_H */
