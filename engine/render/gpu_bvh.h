#ifndef PATCH_RENDER_GPU_BVH_H
#define PATCH_RENDER_GPU_BVH_H

#include <cstdint>

namespace patch
{

static constexpr int32_t GPU_BVH_MAX_NODES = 1023;
static constexpr int32_t GPU_BVH_MAX_OBJECTS = 512;

struct alignas(16) GPUBVHParams
{
    int32_t node_count;
    int32_t object_count;
    int32_t root_index;
    int32_t _pad0;
    float scene_bounds_min[4];
    float scene_bounds_max[4];
};
static_assert(sizeof(GPUBVHParams) == 48, "GPUBVHParams must be 48 bytes");

struct alignas(32) GPUBVHNode
{
    float aabb_min[3];
    int32_t left_first;
    float aabb_max[3];
    int32_t count;
};
static_assert(sizeof(GPUBVHNode) == 32, "GPUBVHNode must be 32 bytes");

struct GPUBVHBuffer
{
    GPUBVHParams params;
    uint32_t _pad_to_64[4];
    GPUBVHNode nodes[GPU_BVH_MAX_NODES];
    int32_t object_indices[GPU_BVH_MAX_OBJECTS];
};
static_assert(sizeof(GPUBVHBuffer) == 48 + 16 + 32 * 1023 + 4 * 512, "GPUBVHBuffer size mismatch");

} // namespace patch

#endif // PATCH_RENDER_GPU_BVH_H
