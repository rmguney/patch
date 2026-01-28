#ifndef BVH_GLSL
#define BVH_GLSL

#define BVH_MAX_NODES 1023
#define BVH_MAX_OBJECTS 512
#define BVH_STACK_SIZE 16
#define BVH_MAX_CANDIDATES 24

struct BVHParams {
    int node_count;
    int object_count;
    int root_index;
    int _pad0;
    vec4 scene_bounds_min;
    vec4 scene_bounds_max;
};

struct BVHNode {
    vec3 aabb_min;
    int left_first;
    vec3 aabb_max;
    int count;
};

#ifndef BVH_SET
#define BVH_SET 1
#endif
#ifndef BVH_BINDING
#define BVH_BINDING 2
#endif

layout(std430, set = BVH_SET, binding = BVH_BINDING) readonly buffer BVHBuffer {
    BVHParams bvh_params;
    uint _bvh_pad[4];
    BVHNode bvh_nodes[BVH_MAX_NODES];
    int bvh_object_indices[BVH_MAX_OBJECTS];
};

#endif // BVH_GLSL
