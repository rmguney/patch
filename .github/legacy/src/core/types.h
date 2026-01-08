#ifndef PATCH_CORE_TYPES_H
#define PATCH_CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float x, y, z, w;
} Vec4;

typedef struct {
    float m[16];
} Mat4;

typedef struct {
    Vec3 position;
    Vec3 normal;
} Vertex;

typedef struct {
    Vec3 position;
    Vec3 velocity;
    Vec3 color;
    float radius;
    float mass;
    bool fragmented;
} Ball;

typedef struct {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
} Bounds3D;

typedef struct {
    Mat4 model;
    Mat4 view;
    Mat4 projection;
    Vec4 color_alpha;
    Vec4 params;
} PushConstants;

typedef struct {
    Mat4 light_view_proj;
    Vec4 light_dir;
} ShadowUniforms;

#define VOXEL_GRID_X 128
#define VOXEL_GRID_Y 64
#define VOXEL_GRID_Z 128
#define VOXEL_TOTAL (VOXEL_GRID_X * VOXEL_GRID_Y * VOXEL_GRID_Z)

typedef struct {
    uint8_t r, g, b;
    uint8_t active;
} Voxel;

typedef struct {
    Mat4 view;
    Mat4 projection;
    Vec3 bounds_min;
    float voxel_size;
    Vec3 bounds_max;
    float pad1;
    Vec3 camera_pos;
    float pad2;
    int32_t grid_x;
    int32_t grid_y;
    int32_t grid_z;
    float pad3;
} VoxelPushConstants;

#ifdef __cplusplus
}
#endif

#endif
