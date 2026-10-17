#ifndef PATCH_RENDER_GPU_BVH_H
#define PATCH_RENDER_GPU_BVH_H

#include <cstddef>
#include <cstdint>

namespace patch
{

/* Mirrors the BVHBuffer storage block in shaders/include/bvh.glsl (std430) */
static constexpr int32_t GPU_BVH_MAX_NODES = 1023;
static constexpr int32_t GPU_BVH_MAX_OBJECTS = 512;

struct GPUBVHParams
{
    int32_t node_count;
    int32_t object_count;
    int32_t pad[2];
};
static_assert(sizeof(GPUBVHParams) == 16, "GPUBVHParams must be 16 bytes");

struct GPUBVHNode
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
    GPUBVHNode nodes[GPU_BVH_MAX_NODES];
    int32_t object_indices[GPU_BVH_MAX_OBJECTS];
};
static_assert(sizeof(GPUBVHBuffer) == 16 + 32 * 1023 + 4 * 512, "GPUBVHBuffer size mismatch");
static_assert(offsetof(GPUBVHBuffer, nodes) == 16, "GPUBVHBuffer.nodes offset");
static_assert(offsetof(GPUBVHBuffer, object_indices) == 32752, "GPUBVHBuffer.object_indices offset");

} // namespace patch

#endif // PATCH_RENDER_GPU_BVH_H
