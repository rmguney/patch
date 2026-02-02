#ifndef PATCH_RENDER_VOXEL_PUSH_CONSTANTS_H
#define PATCH_RENDER_VOXEL_PUSH_CONSTANTS_H

/* Quality setting defaults */
#define QUALITY_DEFAULT_SHADOW 1         /* 0=None, 1=Fair, 2=Good, 3=High */
#define QUALITY_DEFAULT_SHADOW_CONTACT 1 /* 0=Off, 1=On */
#define QUALITY_DEFAULT_AO 0             /* 0=None, 1=Fair, 2=Good, 3=High */
#define QUALITY_DEFAULT_LOD 0            /* 0=Fair, 1=Good, 2=High */
#define QUALITY_DEFAULT_DENOISE 1        /* 0=Off, 1=On */
#define QUALITY_DEFAULT_TAA 1            /* 0=Off, 1=On */
#define QUALITY_DEFAULT_GI 0             /* 0=Off, 2=Good, 3=High */

/* Quality preset enumeration */
typedef enum
{
    QUALITY_PRESET_DEFAULT = 0,
    QUALITY_PRESET_FAIR = 1,
    QUALITY_PRESET_GOOD = 2,
    QUALITY_PRESET_HIGH = 3,
    QUALITY_PRESET_CUSTOM = 4
} QualityPreset;

/* Quality preset settings */
typedef struct
{
    int32_t shadow;
    int32_t shadow_contact;
    int32_t ao;
    int32_t lod;
    int32_t denoise;
    int32_t taa;
    int32_t gi;
} QualityPresetSettings;

static const QualityPresetSettings QUALITY_PRESETS[] = {
    {1, 0, 0, 0, 1, 1, 0}, /* Default: GI off */
    {1, 1, 1, 1, 1, 1, 0}, /* Fair: GI off */
    {2, 1, 2, 1, 1, 1, 2}, /* Good: GI 4 cones */
    {3, 1, 3, 2, 1, 1, 3}  /* High: GI 6 cones */
};

/*
 * Voxel raymarching push constants (256 bytes).
 *
 * Push constants are the fastest path for per-draw shader data—no descriptor
 * binding, no buffer allocation. We target 256 bytes (supported by all desktop
 * GPUs) rather than the 128-byte Vulkan minimum.
 *
 * Layout rationale:
 * - inv_view/inv_projection: precomputed on CPU to avoid per-fragment inverse
 * - history_valid: bit 0 = temporal history valid
 * - shadow_quality/shadow_contact/ao_quality/lod_quality: individual quality controls
 * - debug_mode: runtime toggle for debug visualization
 *
 * Data exceeding 256 bytes (e.g., prev_view_proj for temporal reprojection)
 * uses a UBO instead.
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
        float chunk_size;
        float camera_pos[3];
        int32_t history_valid;
        int32_t grid_size[3];
        int32_t total_chunks;
        int32_t chunks_dim[3];
        int32_t frame_count;
        int32_t object_shadow_quality;
        int32_t debug_mode;
        int32_t is_orthographic;
        int32_t max_steps;
        float near_plane;
        float far_plane;
        int32_t object_count;
        int32_t shadow_quality;
        int32_t shadow_contact;
        int32_t ao_quality;
        int32_t lod_quality;
        int32_t taa_quality;
    };

    /*
     * Temporal UBO for data exceeding push constant limits.
     * Contains previous and current view-projection for reprojection and SSR.
     */
    struct VoxelTemporalUBO
    {
        Mat4 prev_view_proj;
        Mat4 view_proj;
        int32_t gi_quality;
        int32_t _reserved[3];
    };

    struct GIInjectPushConstants
    {
        float bounds_min[3];
        float voxel_size;
        float bounds_max[3];
        float radiance_voxel_size;
        int32_t grid_size[3];
        int32_t total_chunks;
        int32_t chunks_dim[3];
        int32_t frame_count;
        int32_t radiance_dims[3];
        int32_t gi_quality;
        float key_light_dir[3];
        float key_light_strength;
        float key_light_color[3];
        float _pad;
    };

    struct GIMipmapPushConstants
    {
        int32_t dst_dims[3];
        int32_t _pad;
    };

    struct GIConePushConstants
    {
        float bounds_min[3];
        float voxel_size;
        float bounds_max[3];
        float radiance_voxel_size;
        int32_t radiance_dims[3];
        int32_t frame_count;
        float cam_pos[3];
        int32_t gi_quality;
        int32_t screen_width;
        int32_t screen_height;
        int32_t _pad[2];
    };

static_assert(sizeof(struct VoxelPushConstants) == 256, "VoxelPushConstants must be 256 bytes");
static_assert(sizeof(struct VoxelTemporalUBO) == 144, "VoxelTemporalUBO must be 144 bytes");
static_assert(sizeof(struct GIInjectPushConstants) == 112, "GIInjectPushConstants must be 112 bytes");
static_assert(sizeof(struct GIMipmapPushConstants) == 16, "GIMipmapPushConstants must be 16 bytes");
static_assert(sizeof(struct GIConePushConstants) == 80, "GIConePushConstants must be 80 bytes");

#ifdef __cplusplus
} // namespace patch
#endif

#endif
