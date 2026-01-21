#pragma once

#include "engine/render/renderer.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"

struct DebugInfo
{
    /* Scene identification */
    const char *scene_name;

    /* Content stats (scene-agnostic) */
    int32_t object_count;
    int32_t particle_count;
    int32_t spawn_count;
    int32_t tick_count;

    /* Volume info */
    int32_t total_chunks;
    int32_t active_chunks;
    int32_t solid_voxels;
    int32_t dirty_queue_count;
    int32_t total_uploaded;
    bool dirty_overflow;
    int32_t chunks_x, chunks_y, chunks_z;
    float voxel_size;

    /* Bounds */
    float bounds_min[3];
    float bounds_max[3];

    /* Renderer state */
    bool gbuffer_init;
    bool gbuffer_pipeline_valid;
    bool gbuffer_descriptors_valid;
    bool voxel_res_init;
    bool vobj_res_init;
    int terrain_debug_mode;
    int terrain_draw_count;

    /* Camera */
    float camera_pos[3];
    float camera_fwd[3];

    /* GPU info */
    const char *gpu_name;
    float gpu_shadow_ms;
    float gpu_main_ms;
    float gpu_total_ms;
    bool gpu_timings_valid;

    /* CPU waits */
    float cpu_fence_ms;
    float cpu_acquire_ms;
    float cpu_present_ms;

    /* Frame timing (from profiler when available) */
    float fps_current;
    float fps_avg;
    float frame_ms_current;
    float frame_ms_avg;
    float frame_ms_max;
    float frame_ms_p50;
    float frame_ms_p95;
    float frame_ms_p99;
    float budget_pct;
    int32_t budget_overruns;
    float budget_worst_ms;

    /* Sim timing */
    float sim_tick_ms;
    float sim_physics_ms;
    float sim_particles_ms;
    float sim_collision_ms;

    /* Render timing */
    float render_total_ms;
    float render_shadow_ms;
    float render_main_ms;
    float render_gbuffer_ms;
    float render_objects_ms;
    float render_lighting_ms;
    float render_ao_ms;
    float render_denoise_ms;
    float render_ui_overlay_ms;
};

struct DebugExportFeedback
{
    char filename[128];
    float timer;
    bool success;
};

void debug_info_clear(DebugInfo *info);

void debug_info_populate_volume(DebugInfo *info, const VoxelVolume *terrain);

void debug_info_populate_objects(DebugInfo *info, const VoxelObjectWorld *objects);

void debug_info_populate_particles(DebugInfo *info, const ParticleSystem *particles);

void debug_info_populate_renderer(DebugInfo *info, const patch::Renderer *renderer);

void debug_info_populate_profiler(DebugInfo *info);

bool export_debug_report(const char *filename, const DebugInfo *info);

bool draw_debug_overlay(patch::Renderer &renderer,
                        int32_t window_width, int32_t window_height,
                        const DebugInfo *info,
                        float mouse_x, float mouse_y, bool mouse_clicked,
                        const DebugExportFeedback *feedback);
