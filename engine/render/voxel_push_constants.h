#ifndef PATCH_RENDER_VOXEL_PUSH_CONSTANTS_H
#define PATCH_RENDER_VOXEL_PUSH_CONSTANTS_H

#include <cstdint>

/* Quality setting defaults */
#define QUALITY_DEFAULT_SHADOW 1         /* 0=None, 1=Fair, 2=Good, 3=High */
#define QUALITY_DEFAULT_SHADOW_CONTACT 1 /* 0=Off, 1=On */
#define QUALITY_DEFAULT_AO 0             /* 0=None, 1=Fair, 2=Good, 3=High */
#define QUALITY_DEFAULT_DENOISE 1        /* 0=Off, 1=On */
#define QUALITY_DEFAULT_TAA 1            /* 0=Off, 1=On */

/* Quality preset enumeration */
enum QualityPreset : uint8_t
{
    QUALITY_PRESET_DEFAULT = 0,
    QUALITY_PRESET_FAIR = 1,
    QUALITY_PRESET_GOOD = 2,
    QUALITY_PRESET_HIGH = 3,
    QUALITY_PRESET_CUSTOM = 4
};

/* Quality preset settings */
typedef struct
{
    int32_t shadow;
    int32_t shadow_contact;
    int32_t ao;
    int32_t denoise;
    int32_t taa;
} QualityPresetSettings;

static const QualityPresetSettings QUALITY_PRESETS[] = {
    {1, 0, 0, 1, 1}, /* Default */
    {1, 1, 1, 1, 1}, /* Fair */
    {2, 1, 2, 1, 1}, /* Good */
    {3, 1, 3, 1, 1}  /* High */
};

/*
 * Push constants shared by the compute raymarching passes and the deferred lighting pass
 * (232 bytes; every shader includes shaders/include/voxel_push_constants.glsl, which must
 * mirror this struct). Each vec3 is paired with a scalar so the std430 layout has no padding.
 *
 * - inv_view/inv_projection: precomputed on CPU to avoid per-pixel inverses
 * - history_valid: temporal history usable (set per temporal pass)
 * - frame_count: frame index modulo 65536 (temporal noise offsets)
 * - debug_mode: runtime debug view (F4/F5)
 *
 * Data exceeding the push constant budget (last frame's camera, for reprojection) uses a UBO.
 */

#include "engine/core/types.h"

#ifdef __cplusplus
namespace patch
{
#endif

    struct VoxelPushConstants
    {
        Mat4 inv_view;
        Mat4 inv_projection;
        float bounds_min[3];
        float voxel_size;
        float bounds_max[3];
        int32_t max_steps;
        float camera_pos[3];
        int32_t history_valid;
        int32_t grid_size[3];
        int32_t total_chunks;
        int32_t chunks_dim[3];
        int32_t frame_count;
        int32_t debug_mode;
        float far_plane;
        int32_t object_count;
        int32_t shadow_quality;
        int32_t shadow_contact;
        int32_t ao_quality;
    };

    /*
     * Temporal UBO for data exceeding push constant limits: last frame's camera (without the TAA
     * jitter) and this frame's jitter, for the motion vectors the G-buffer passes write and the
     * disocclusion test of temporal_ao.comp. std140.
     */
    struct VoxelTemporalUBO
    {
        Mat4 prev_view_proj;
        Vec4 prev_camera_pos;    /* .w = 1 */
        float jitter_uv[2];      /* this frame's projection offset, in uv: a pixel centre minus it is unjittered */
        float inv_render_size[2]; /* 1 / the render target's size in pixels */
    };

    struct SceneLightingUBO
    {
        Vec4 sun_dir;        /* .w = wrap_bias */
        Vec4 sun_color;      /* .w = strength */
        Vec4 fill_dir;       /* .w = strength */
        Vec4 fill_color;     /* .w = back_strength */
        Vec4 back_dir;       /* .w = rim_strength */
        Vec4 back_color;     /* .w = ambient_strength */
        Vec4 sky_ambient;    /* .w = exposure */
        Vec4 ground_ambient; /* .w = emissive_mult */
        Vec4 sky_color;      /* .w = shadow_max_dist */
    };

static_assert(sizeof(struct VoxelPushConstants) == 232, "VoxelPushConstants must be 232 bytes");
static_assert(sizeof(struct VoxelTemporalUBO) == 96, "VoxelTemporalUBO must be 96 bytes");
static_assert(sizeof(struct SceneLightingUBO) == 144, "SceneLightingUBO must be 144 bytes");

#ifdef __cplusplus
} // namespace patch
#endif

#endif
