#ifndef BVH_TRAVERSE_GLSL
#define BVH_TRAVERSE_GLSL

#include "bvh.glsl"

/* Entry distance of the ray into the AABB, or 1e30 on a miss */
float bvh_ray_aabb(vec3 ro, vec3 inv_rd, vec3 aabb_min, vec3 aabb_max) {
    vec3 t0 = (aabb_min - ro) * inv_rd;
    vec3 t1 = (aabb_max - ro) * inv_rd;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float enter = max(max(tmin.x, tmin.y), tmin.z);
    float exit = min(min(tmax.x, tmax.y), tmax.z);
    return (enter <= exit && exit > 0.0) ? max(enter, 0.0) : 1e30;
}

/*
 * Closest voxel object hit along the ray, or max_t. object_count bounds the object
 * indices to the uploaded metadata (the CPU BVH can hold objects beyond the metadata slots).
 */
float bvh_trace_object_shadow(vec3 origin, vec3 dir, float max_t, int object_count) {
    if (bvh_params.object_count <= 0 || object_count <= 0) return max_t;

    vec3 inv_dir = 1.0 / dir;
    int stack[BVH_STACK_SIZE];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;

    float closest_t = max_t;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        BVHNode node = bvh_nodes[node_idx];

        float t_hit = bvh_ray_aabb(origin, inv_dir, node.aabb_min, node.aabb_max);
        if (t_hit > closest_t) continue;

        if (node.count > 0) {
            for (int i = 0; i < node.count; i++) {
                int obj_idx = bvh_object_indices[node.left_first + i];
                if (obj_idx >= object_count || !vobj_is_active(obj_idx)) continue;

                HitInfo hit = vobj_march_object(obj_idx, origin, dir, closest_t);
                if (hit.hit && hit.t < closest_t) {
                    closest_t = hit.t;
                }
            }
        } else {
            int left_idx = node.left_first;
            int right_idx = node.left_first + 1;

            float t_left = bvh_ray_aabb(origin, inv_dir, bvh_nodes[left_idx].aabb_min, bvh_nodes[left_idx].aabb_max);
            float t_right = bvh_ray_aabb(origin, inv_dir, bvh_nodes[right_idx].aabb_min, bvh_nodes[right_idx].aabb_max);

            /* Push the farther child first so the nearer one is visited next */
            if (t_left < t_right) {
                if (t_right <= closest_t && stack_ptr < BVH_STACK_SIZE) stack[stack_ptr++] = right_idx;
                if (t_left <= closest_t && stack_ptr < BVH_STACK_SIZE) stack[stack_ptr++] = left_idx;
            } else {
                if (t_left <= closest_t && stack_ptr < BVH_STACK_SIZE) stack[stack_ptr++] = left_idx;
                if (t_right <= closest_t && stack_ptr < BVH_STACK_SIZE) stack[stack_ptr++] = right_idx;
            }
        }
    }

    return closest_t;
}

#endif // BVH_TRAVERSE_GLSL
