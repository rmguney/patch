#ifndef PATCH_CORE_TYPES_H
#define PATCH_CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float x, y, z;
    } Vec3;

    typedef struct
    {
        float x, y, z, w;
    } Vec4;

    typedef struct
    {
        float m[16];
    } Mat4;

    typedef struct
    {
        float x, y, z, w;
    } Quat;

    typedef struct
    {
        Vec3 position;
        Vec3 normal;
    } Vertex;

    typedef struct
    {
        float min_x, max_x;
        float min_y, max_y;
        float min_z, max_z;
    } Bounds3D;

    typedef struct
    {
        Mat4 model;
        Mat4 view;
        Mat4 projection;
        Vec4 color_alpha;
        Vec4 params;
    } PushConstants;

    typedef struct
    {
        Mat4 light_view_proj;
        Vec4 light_dir;
    } ShadowUniforms;

/* Voxel material ID 0 is reserved for empty/air (engine constant, not content) */
#define VOXEL_MATERIAL_EMPTY 0

/* Maximum materials supported by the voxel system */
#define VOXEL_MATERIAL_MAX 256

/*
 * CPU↔GPU shared struct size invariants.
 * These must match shader expectations exactly; mismatches cause UB on upload.
 */
static_assert(sizeof(Vec3) == 12, "Vec3 must be 12 bytes for GPU alignment");
static_assert(sizeof(Vec4) == 16, "Vec4 must be 16 bytes for GPU alignment");
static_assert(sizeof(Mat4) == 64, "Mat4 must be 64 bytes for GPU alignment");
static_assert(sizeof(PushConstants) == 224, "PushConstants size mismatch with shader");
static_assert(sizeof(ShadowUniforms) == 80, "ShadowUniforms size mismatch with shader");

#ifdef __cplusplus
}
#endif

#endif
