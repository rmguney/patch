#pragma once

#include "engine/render/renderer.h"
#include "game/ball_pit.h"

struct DebugSceneInfo
{
    /* Content */
    int32_t object_count;
    int32_t total_chunks;
    int32_t solid_voxels;
    int32_t active_chunks;
    int32_t dirty_queue_count;
    int32_t total_uploaded;
    bool dirty_overflow;

    /* Renderer state */
    bool gbuffer_init;
    bool gbuffer_pipeline_valid;
    bool gbuffer_descriptors_valid;
    bool voxel_res_init;
    bool vobj_res_init;
    int terrain_debug_mode;
    int terrain_draw_count;

    /* Bounds */
    float bounds_min[3];
    float bounds_max[3];

    /* Camera */
    float camera_pos[3];

    /* Volume info */
    int32_t chunks_x, chunks_y, chunks_z;
    float voxel_size;
};

struct DebugExportFeedback
{
    char filename[128];
    float timer;
    bool success;
};

#ifdef PATCH_PROFILE
void export_profile_csv(const char *filename, const patch::Renderer *renderer);
#endif

bool export_debug_info(const char *filename, const DebugSceneInfo *info, float fps);

bool export_all_debug(const char *debug_filename, const char *profile_filename,
                      const DebugSceneInfo *info, float fps, const patch::Renderer *renderer);

bool draw_overlay(patch::Renderer &renderer, float fps, const BallPitStats *stats,
                  int32_t window_width, int32_t window_height,
                  const DebugSceneInfo *dbg,
                  float mouse_x, float mouse_y, bool mouse_clicked,
                  const DebugExportFeedback *feedback);
