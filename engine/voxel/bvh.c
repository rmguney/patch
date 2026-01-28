#include "bvh.h"
#include "voxel_object.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define BVH_SAH_TRAVERSAL_COST 1.0f
#define BVH_SAH_INTERSECTION_COST 2.0f
#define BVH_STACK_SIZE 32

typedef struct
{
    float aabb_min[3];
    float aabb_max[3];
    int32_t count;
} SAHBin;

static float compute_surface_area(const float min[3], const float max[3])
{
    float dx = max[0] - min[0];
    float dy = max[1] - min[1];
    float dz = max[2] - min[2];
    if (dx <= 0.0f || dy <= 0.0f || dz <= 0.0f)
        return 0.0f;
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

static void update_node_bounds(BVH *bvh, int32_t node_idx)
{
    BVHNode *node = &bvh->nodes[node_idx];
    float min_bound[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float max_bound[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (int32_t i = 0; i < node->count; i++)
    {
        int32_t obj_idx = bvh->object_indices[node->left_first + i];
        for (int32_t j = 0; j < 3; j++)
        {
            if (bvh->obj_aabb_min[obj_idx][j] < min_bound[j])
                min_bound[j] = bvh->obj_aabb_min[obj_idx][j];
            if (bvh->obj_aabb_max[obj_idx][j] > max_bound[j])
                max_bound[j] = bvh->obj_aabb_max[obj_idx][j];
        }
    }

    for (int32_t j = 0; j < 3; j++)
    {
        node->aabb_min[j] = min_bound[j];
        node->aabb_max[j] = max_bound[j];
    }
}

static float get_centroid_axis(const BVH *bvh, int32_t obj_idx, int32_t axis)
{
    switch (axis)
    {
    case 0:
        return bvh->obj_centroids[obj_idx].x;
    case 1:
        return bvh->obj_centroids[obj_idx].y;
    default:
        return bvh->obj_centroids[obj_idx].z;
    }
}

static float find_best_split(BVH *bvh, int32_t node_idx, int32_t *out_axis, float *out_pos)
{
    BVHNode *node = &bvh->nodes[node_idx];
    float best_cost = FLT_MAX;
    *out_axis = -1;
    *out_pos = 0.0f;

    for (int32_t axis = 0; axis < 3; axis++)
    {
        float cent_min = FLT_MAX, cent_max = -FLT_MAX;
        for (int32_t i = 0; i < node->count; i++)
        {
            int32_t obj_idx = bvh->object_indices[node->left_first + i];
            float cent = get_centroid_axis(bvh, obj_idx, axis);
            if (cent < cent_min)
                cent_min = cent;
            if (cent > cent_max)
                cent_max = cent;
        }

        if (cent_max - cent_min < 0.001f)
            continue;

        SAHBin bins[BVH_SAH_BINS];
        for (int32_t b = 0; b < BVH_SAH_BINS; b++)
        {
            bins[b].aabb_min[0] = bins[b].aabb_min[1] = bins[b].aabb_min[2] = FLT_MAX;
            bins[b].aabb_max[0] = bins[b].aabb_max[1] = bins[b].aabb_max[2] = -FLT_MAX;
            bins[b].count = 0;
        }

        float bin_scale = (float)BVH_SAH_BINS / (cent_max - cent_min);
        for (int32_t i = 0; i < node->count; i++)
        {
            int32_t obj_idx = bvh->object_indices[node->left_first + i];
            float cent = get_centroid_axis(bvh, obj_idx, axis);
            int32_t bin_idx = (int32_t)((cent - cent_min) * bin_scale);
            if (bin_idx >= BVH_SAH_BINS)
                bin_idx = BVH_SAH_BINS - 1;

            bins[bin_idx].count++;
            for (int32_t j = 0; j < 3; j++)
            {
                if (bvh->obj_aabb_min[obj_idx][j] < bins[bin_idx].aabb_min[j])
                    bins[bin_idx].aabb_min[j] = bvh->obj_aabb_min[obj_idx][j];
                if (bvh->obj_aabb_max[obj_idx][j] > bins[bin_idx].aabb_max[j])
                    bins[bin_idx].aabb_max[j] = bvh->obj_aabb_max[obj_idx][j];
            }
        }

        float left_area[BVH_SAH_BINS - 1], right_area[BVH_SAH_BINS - 1];
        int32_t left_count[BVH_SAH_BINS - 1], right_count[BVH_SAH_BINS - 1];

        float acc_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
        float acc_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        int32_t acc_count = 0;
        for (int32_t b = 0; b < BVH_SAH_BINS - 1; b++)
        {
            acc_count += bins[b].count;
            for (int32_t j = 0; j < 3; j++)
            {
                if (bins[b].aabb_min[j] < acc_min[j])
                    acc_min[j] = bins[b].aabb_min[j];
                if (bins[b].aabb_max[j] > acc_max[j])
                    acc_max[j] = bins[b].aabb_max[j];
            }
            left_count[b] = acc_count;
            left_area[b] = compute_surface_area(acc_min, acc_max);
        }

        acc_min[0] = acc_min[1] = acc_min[2] = FLT_MAX;
        acc_max[0] = acc_max[1] = acc_max[2] = -FLT_MAX;
        acc_count = 0;
        for (int32_t b = BVH_SAH_BINS - 1; b > 0; b--)
        {
            acc_count += bins[b].count;
            for (int32_t j = 0; j < 3; j++)
            {
                if (bins[b].aabb_min[j] < acc_min[j])
                    acc_min[j] = bins[b].aabb_min[j];
                if (bins[b].aabb_max[j] > acc_max[j])
                    acc_max[j] = bins[b].aabb_max[j];
            }
            right_count[b - 1] = acc_count;
            right_area[b - 1] = compute_surface_area(acc_min, acc_max);
        }

        float parent_area = compute_surface_area(node->aabb_min, node->aabb_max);
        if (parent_area < 0.0001f)
            continue;
        float scale = BVH_SAH_INTERSECTION_COST / parent_area;

        for (int32_t b = 0; b < BVH_SAH_BINS - 1; b++)
        {
            if (left_count[b] == 0 || right_count[b] == 0)
                continue;
            float cost = BVH_SAH_TRAVERSAL_COST +
                         scale * ((float)left_count[b] * left_area[b] + (float)right_count[b] * right_area[b]);
            if (cost < best_cost)
            {
                best_cost = cost;
                *out_axis = axis;
                *out_pos = cent_min + (b + 1) * (cent_max - cent_min) / BVH_SAH_BINS;
            }
        }
    }

    return best_cost;
}

static void subdivide(BVH *bvh, int32_t node_idx)
{
    BVHNode *node = &bvh->nodes[node_idx];

    if (node->count <= BVH_LEAF_MAX_OBJECTS)
        return;

    int32_t split_axis;
    float split_pos;
    float split_cost = find_best_split(bvh, node_idx, &split_axis, &split_pos);

    float no_split_cost = (float)node->count * BVH_SAH_INTERSECTION_COST;
    if (split_axis < 0 || split_cost >= no_split_cost)
        return;

    int32_t left_start = node->left_first;
    int32_t left_end = left_start;
    int32_t right_start = left_start + node->count - 1;

    while (left_end <= right_start)
    {
        int32_t obj_idx = bvh->object_indices[left_end];
        float cent = get_centroid_axis(bvh, obj_idx, split_axis);
        if (cent < split_pos)
        {
            left_end++;
        }
        else
        {
            int32_t tmp = bvh->object_indices[left_end];
            bvh->object_indices[left_end] = bvh->object_indices[right_start];
            bvh->object_indices[right_start] = tmp;
            right_start--;
        }
    }

    int32_t left_count = left_end - left_start;
    if (left_count == 0 || left_count == node->count)
        return;

    int32_t left_idx = bvh->node_count++;
    int32_t right_idx = bvh->node_count++;

    bvh->nodes[left_idx].left_first = left_start;
    bvh->nodes[left_idx].count = left_count;
    bvh->nodes[right_idx].left_first = left_end;
    bvh->nodes[right_idx].count = node->count - left_count;

    node->left_first = left_idx;
    node->count = 0;

    update_node_bounds(bvh, left_idx);
    update_node_bounds(bvh, right_idx);

    subdivide(bvh, left_idx);
    subdivide(bvh, right_idx);
}

BVH *bvh_create(void)
{
    BVH *bvh = (BVH *)calloc(1, sizeof(BVH));
    return bvh;
}

void bvh_destroy(BVH *bvh)
{
    if (bvh)
        free(bvh);
}

void bvh_build(BVH *bvh, const VoxelObjectWorld *world)
{
    bvh->node_count = 0;
    bvh->object_count = 0;

    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS && bvh->object_count < BVH_MAX_OBJECTS; i++)
    {
        const VoxelObject *obj = &world->objects[i];
        if (!obj->active)
            continue;

        int32_t idx = bvh->object_count++;
        bvh->object_indices[idx] = i;

        /* Store AABBs/centroids at WORLD index (i), not BVH-internal index (idx).
           update_node_bounds reads via object_indices[] which yields world indices. */
        float r = obj->radius;
        bvh->obj_centroids[i] = obj->position;
        bvh->obj_aabb_min[i][0] = obj->position.x - r;
        bvh->obj_aabb_min[i][1] = obj->position.y - r;
        bvh->obj_aabb_min[i][2] = obj->position.z - r;
        bvh->obj_aabb_max[i][0] = obj->position.x + r;
        bvh->obj_aabb_max[i][1] = obj->position.y + r;
        bvh->obj_aabb_max[i][2] = obj->position.z + r;
    }

    if (bvh->object_count == 0)
        return;

    BVHNode *root = &bvh->nodes[bvh->node_count++];
    root->left_first = 0;
    root->count = bvh->object_count;
    update_node_bounds(bvh, 0);

    subdivide(bvh, 0);
}

void bvh_refit(BVH *bvh, const VoxelObjectWorld *world)
{
    for (int32_t i = 0; i < bvh->object_count; i++)
    {
        int32_t world_idx = bvh->object_indices[i];
        const VoxelObject *obj = &world->objects[world_idx];
        float r = obj->radius;

        /* Store at WORLD index to match update_node_bounds which reads via object_indices */
        bvh->obj_centroids[world_idx] = obj->position;
        bvh->obj_aabb_min[world_idx][0] = obj->position.x - r;
        bvh->obj_aabb_min[world_idx][1] = obj->position.y - r;
        bvh->obj_aabb_min[world_idx][2] = obj->position.z - r;
        bvh->obj_aabb_max[world_idx][0] = obj->position.x + r;
        bvh->obj_aabb_max[world_idx][1] = obj->position.y + r;
        bvh->obj_aabb_max[world_idx][2] = obj->position.z + r;
    }

    for (int32_t i = bvh->node_count - 1; i >= 0; i--)
    {
        BVHNode *node = &bvh->nodes[i];

        if (node->count > 0)
        {
            update_node_bounds(bvh, i);
        }
        else
        {
            BVHNode *left = &bvh->nodes[node->left_first];
            BVHNode *right = &bvh->nodes[node->left_first + 1];

            for (int32_t j = 0; j < 3; j++)
            {
                node->aabb_min[j] = left->aabb_min[j] < right->aabb_min[j] ? left->aabb_min[j] : right->aabb_min[j];
                node->aabb_max[j] = left->aabb_max[j] > right->aabb_max[j] ? left->aabb_max[j] : right->aabb_max[j];
            }
        }
    }
}

bool bvh_needs_rebuild(const BVH *bvh, const VoxelObjectWorld *world)
{
    int32_t active_count = 0;
    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
    {
        if (world->objects[i].active)
            active_count++;
    }

    if (active_count != bvh->object_count)
        return true;

    for (int32_t i = 0; i < bvh->object_count; i++)
    {
        int32_t world_idx = bvh->object_indices[i];
        if (world_idx >= VOBJ_MAX_OBJECTS || !world->objects[world_idx].active)
            return true;
    }

    return false;
}

static float ray_aabb_intersect(Vec3 origin, Vec3 inv_dir, const float aabb_min[3], const float aabb_max[3])
{
    float t0x = (aabb_min[0] - origin.x) * inv_dir.x;
    float t1x = (aabb_max[0] - origin.x) * inv_dir.x;
    float t0y = (aabb_min[1] - origin.y) * inv_dir.y;
    float t1y = (aabb_max[1] - origin.y) * inv_dir.y;
    float t0z = (aabb_min[2] - origin.z) * inv_dir.z;
    float t1z = (aabb_max[2] - origin.z) * inv_dir.z;

    float tmin_x = t0x < t1x ? t0x : t1x;
    float tmax_x = t0x > t1x ? t0x : t1x;
    float tmin_y = t0y < t1y ? t0y : t1y;
    float tmax_y = t0y > t1y ? t0y : t1y;
    float tmin_z = t0z < t1z ? t0z : t1z;
    float tmax_z = t0z > t1z ? t0z : t1z;

    float tmin = tmin_x > tmin_y ? tmin_x : tmin_y;
    tmin = tmin > tmin_z ? tmin : tmin_z;
    float tmax = tmax_x < tmax_y ? tmax_x : tmax_y;
    tmax = tmax < tmax_z ? tmax : tmax_z;

    if (tmin <= tmax && tmax > 0.0f)
        return tmin > 0.0f ? tmin : 0.0f;
    return FLT_MAX;
}

int32_t bvh_query_ray_candidates(const BVH *bvh, Vec3 origin, Vec3 dir, float max_dist,
                                 int32_t *out_indices, int32_t max_results)
{
    if (bvh->node_count <= 0)
        return 0;

    Vec3 inv_dir = vec3_create(
        fabsf(dir.x) > K_EPSILON ? 1.0f / dir.x : (dir.x >= 0 ? FLT_MAX : -FLT_MAX),
        fabsf(dir.y) > K_EPSILON ? 1.0f / dir.y : (dir.y >= 0 ? FLT_MAX : -FLT_MAX),
        fabsf(dir.z) > K_EPSILON ? 1.0f / dir.z : (dir.z >= 0 ? FLT_MAX : -FLT_MAX));

    int32_t stack[BVH_STACK_SIZE];
    int32_t stack_ptr = 0;
    stack[stack_ptr++] = 0;

    int32_t count = 0;

    while (stack_ptr > 0 && count < max_results)
    {
        int32_t node_idx = stack[--stack_ptr];
        const BVHNode *node = &bvh->nodes[node_idx];

        float t_hit = ray_aabb_intersect(origin, inv_dir, node->aabb_min, node->aabb_max);
        if (t_hit > max_dist)
            continue;

        if (node->count > 0)
        {
            for (int32_t i = 0; i < node->count && count < max_results; i++)
            {
                out_indices[count++] = bvh->object_indices[node->left_first + i];
            }
        }
        else
        {
            int32_t left_idx = node->left_first;
            int32_t right_idx = node->left_first + 1;

            float t_left = ray_aabb_intersect(origin, inv_dir, bvh->nodes[left_idx].aabb_min, bvh->nodes[left_idx].aabb_max);
            float t_right = ray_aabb_intersect(origin, inv_dir, bvh->nodes[right_idx].aabb_min, bvh->nodes[right_idx].aabb_max);

            if (t_left < t_right)
            {
                if (t_right <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = right_idx;
                if (t_left <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = left_idx;
            }
            else
            {
                if (t_left <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = left_idx;
                if (t_right <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = right_idx;
            }
        }
    }

    return count;
}

static bool sphere_aabb_intersect(Vec3 center, float radius, const float aabb_min[3], const float aabb_max[3])
{
    float dist_sq = 0.0f;

    if (center.x < aabb_min[0])
    {
        float d = aabb_min[0] - center.x;
        dist_sq += d * d;
    }
    else if (center.x > aabb_max[0])
    {
        float d = center.x - aabb_max[0];
        dist_sq += d * d;
    }

    if (center.y < aabb_min[1])
    {
        float d = aabb_min[1] - center.y;
        dist_sq += d * d;
    }
    else if (center.y > aabb_max[1])
    {
        float d = center.y - aabb_max[1];
        dist_sq += d * d;
    }

    if (center.z < aabb_min[2])
    {
        float d = aabb_min[2] - center.z;
        dist_sq += d * d;
    }
    else if (center.z > aabb_max[2])
    {
        float d = center.z - aabb_max[2];
        dist_sq += d * d;
    }

    return dist_sq <= radius * radius;
}

BVHQueryResult bvh_query_sphere(const BVH *bvh, Vec3 center, float radius)
{
    BVHQueryResult result = {0};

    if (bvh->node_count <= 0)
        return result;

    int32_t stack[BVH_STACK_SIZE];
    int32_t stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0 && result.count < 64)
    {
        int32_t node_idx = stack[--stack_ptr];
        const BVHNode *node = &bvh->nodes[node_idx];

        if (!sphere_aabb_intersect(center, radius, node->aabb_min, node->aabb_max))
            continue;

        if (node->count > 0)
        {
            for (int32_t i = 0; i < node->count && result.count < 64; i++)
            {
                result.indices[result.count++] = bvh->object_indices[node->left_first + i];
            }
        }
        else
        {
            if (stack_ptr < BVH_STACK_SIZE - 1)
            {
                stack[stack_ptr++] = node->left_first;
                stack[stack_ptr++] = node->left_first + 1;
            }
        }
    }

    return result;
}

static bool aabb_aabb_intersect(Vec3 a_min, Vec3 a_max, const float b_min[3], const float b_max[3])
{
    if (a_max.x < b_min[0] || a_min.x > b_max[0])
        return false;
    if (a_max.y < b_min[1] || a_min.y > b_max[1])
        return false;
    if (a_max.z < b_min[2] || a_min.z > b_max[2])
        return false;
    return true;
}

BVHQueryResult bvh_query_aabb(const BVH *bvh, Vec3 query_min, Vec3 query_max)
{
    BVHQueryResult result = {0};

    if (bvh->node_count <= 0)
        return result;

    int32_t stack[BVH_STACK_SIZE];
    int32_t stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0 && result.count < 64)
    {
        int32_t node_idx = stack[--stack_ptr];
        const BVHNode *node = &bvh->nodes[node_idx];

        if (!aabb_aabb_intersect(query_min, query_max, node->aabb_min, node->aabb_max))
            continue;

        if (node->count > 0)
        {
            for (int32_t i = 0; i < node->count && result.count < 64; i++)
            {
                result.indices[result.count++] = bvh->object_indices[node->left_first + i];
            }
        }
        else
        {
            if (stack_ptr < BVH_STACK_SIZE - 1)
            {
                stack[stack_ptr++] = node->left_first;
                stack[stack_ptr++] = node->left_first + 1;
            }
        }
    }

    return result;
}
