#ifndef BVH_GLSL
#define BVH_GLSL

/* Mirror of patch::GPUBVHBuffer (engine/render/gpu_bvh.h), std430 */
#define BVH_MAX_NODES 1023
#define BVH_MAX_OBJECTS 512
#define BVH_STACK_SIZE 16

struct BVHParams {
    int node_count;
    int object_count;
    int pad0;
    int pad1;
};

/* count > 0: leaf with count objects at bvh_object_indices[left_first];
 * count == 0: inner node with children at left_first and left_first + 1 */
struct BVHNode {
    vec3 aabb_min;
    int left_first;
    vec3 aabb_max;
    int count;
};

/* The including shader defines BVH_SET and BVH_BINDING */
layout(std430, set = BVH_SET, binding = BVH_BINDING) readonly buffer BVHBuffer {
    BVHParams bvh_params;
    BVHNode bvh_nodes[BVH_MAX_NODES];
    int bvh_object_indices[BVH_MAX_OBJECTS];
};

#endif // BVH_GLSL
