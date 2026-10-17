#ifndef PATCH_VOXEL_BVH_H
#define PATCH_VOXEL_BVH_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BVH_MAX_OBJECTS 512
#define BVH_MAX_NODES 1023
#define BVH_LEAF_MAX_OBJECTS 4
#define BVH_SAH_BINS 8
/* Deepest leaf. A depth-first traversal holds at most depth + 1 nodes, so this keeps
 * every query within the 16-entry stack of the GPU traversal (shaders/include/bvh.glsl). */
#define BVH_MAX_DEPTH 15
#define BVH_QUERY_MAX_RESULTS 64

    /*
     * BVH node structure - 32 bytes, GPU cache-line aligned.
     * Used by both CPU (C code) and GPU (GLSL std430).
     *
     * Layout matches GLSL:
     *   struct BVHNode {
     *       vec3 aabb_min;   // 12 bytes
     *       int left_first;  // 4 bytes
     *       vec3 aabb_max;   // 12 bytes
     *       int count;       // 4 bytes
     *   };
     *
     * Encoding:
     *   count > 0: Leaf node with 'count' objects starting at object_indices[left_first]
     *   count == 0: Internal node with left child at nodes[left_first], right at nodes[left_first+1]
     */
    typedef struct
    {
        float aabb_min[3];
        int32_t left_first;
        float aabb_max[3];
        int32_t count;
    } BVHNode;

    STATIC_ASSERT(sizeof(BVHNode) == 32, "BVHNode must be 32 bytes for GPU alignment");

    /*
     * BVH container with cached per-object data for fast refit.
     * The nodes and object_indices arrays are uploaded to GPU as-is.
     */
    typedef struct BVH
    {
        BVHNode nodes[BVH_MAX_NODES];
        int32_t object_indices[BVH_MAX_OBJECTS];
        int32_t node_count;
        int32_t object_count;

        Vec3 obj_centroids[BVH_MAX_OBJECTS];
        float obj_aabb_min[BVH_MAX_OBJECTS][3];
        float obj_aabb_max[BVH_MAX_OBJECTS][3];
    } BVH;

    typedef struct
    {
        int32_t indices[BVH_QUERY_MAX_RESULTS];
        int32_t count;
    } BVHQueryResult;

    struct VoxelObjectWorld;

    BVH *bvh_create(void);
    void bvh_destroy(BVH *bvh);

    /* positions: each object's position by world index, the centre of its bounding sphere
     * (the renderer's BVH bounds the poses objects are drawn at); NULL: the objects' own */
    void bvh_build(BVH *bvh, const struct VoxelObjectWorld *world, const Vec3 *positions);
    void bvh_refit(BVH *bvh, const struct VoxelObjectWorld *world, const Vec3 *positions);
    bool bvh_needs_rebuild(const BVH *bvh, const struct VoxelObjectWorld *world);

    int32_t bvh_query_ray_candidates(const BVH *bvh, Vec3 origin, Vec3 dir, float max_dist,
                                     int32_t *out_indices, int32_t max_results);
    BVHQueryResult bvh_query_sphere(const BVH *bvh, Vec3 center, float radius);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_VOXEL_BVH_H */
