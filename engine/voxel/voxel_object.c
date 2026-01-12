#include "voxel_object.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

int32_t voxel_object_world_alloc_slot(VoxelObjectWorld *world)
{
    for (int32_t i = 0; i < world->object_count; i++)
    {
        if (!world->objects[i].active)
        {
            return i;
        }
    }
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;
    return world->object_count++;
}

void voxel_object_recalc_shape(VoxelObject *obj)
{
    if (obj->voxel_count <= 0)
    {
        obj->active = false;
        obj->shape_dirty = false;
        return;
    }

    int32_t min_x = VOBJ_GRID_SIZE, max_x = 0;
    int32_t min_y = VOBJ_GRID_SIZE, max_y = 0;
    int32_t min_z = VOBJ_GRID_SIZE, max_z = 0;
    float com_x = 0.0f, com_y = 0.0f, com_z = 0.0f;
    int32_t counted = 0;
    uint8_t occupancy = 0;

    /* Single pass: bounds, center of mass, and occupancy */
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                if (obj->voxels[vobj_index(x, y, z)].material != 0)
                {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                    if (z < min_z) min_z = z;
                    if (z > max_z) max_z = z;
                    com_x += (float)x + 0.5f;
                    com_y += (float)y + 0.5f;
                    com_z += (float)z + 0.5f;
                    counted++;

                    /* Occupancy: which 8³ region contains this voxel */
                    int32_t region = (x / 8) + ((y / 8) * 2) + ((z / 8) * 4);
                    occupancy |= (uint8_t)(1 << region);
                }
            }
        }
    }

    obj->occupancy_mask = occupancy;

    float extent_x = (float)(max_x - min_x + 1) * obj->voxel_size * 0.5f;
    float extent_y = (float)(max_y - min_y + 1) * obj->voxel_size * 0.5f;
    float extent_z = (float)(max_z - min_z + 1) * obj->voxel_size * 0.5f;
    obj->shape_half_extents = vec3_create(extent_x, extent_y, extent_z);

    float inv_count = 1.0f / (float)counted;
    com_x *= inv_count;
    com_y *= inv_count;
    com_z *= inv_count;

    /* Radius: use bounding box corners instead of per-voxel corners */
    float max_dist_sq = 0.0f;
    for (int32_t c = 0; c < 8; c++)
    {
        float cx = ((c & 1) ? (float)max_x + 1.0f : (float)min_x);
        float cy = ((c & 2) ? (float)max_y + 1.0f : (float)min_y);
        float cz = ((c & 4) ? (float)max_z + 1.0f : (float)min_z);
        float dx = (cx - com_x) * obj->voxel_size;
        float dy = (cy - com_y) * obj->voxel_size;
        float dz = (cz - com_z) * obj->voxel_size;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > max_dist_sq)
            max_dist_sq = dist_sq;
    }
    obj->radius = sqrtf(max_dist_sq);
    obj->shape_dirty = false;
}

void voxel_object_mark_dirty(VoxelObject *obj)
{
    obj->shape_dirty = true;
}

VoxelObjectWorld *voxel_object_world_create(Bounds3D bounds, float voxel_size)
{
    VoxelObjectWorld *world = (VoxelObjectWorld *)calloc(1, sizeof(VoxelObjectWorld));
    if (!world)
        return NULL;

    world->bounds = bounds;
    world->voxel_size = voxel_size;
    world->object_count = 0;
    world->terrain = NULL;

    return world;
}

void voxel_object_world_destroy(VoxelObjectWorld *world)
{
    if (world)
    {
        free(world);
    }
}

void voxel_object_world_set_terrain(VoxelObjectWorld *world, VoxelVolume *terrain)
{
    if (world)
    {
        world->terrain = terrain;
    }
}

int32_t voxel_object_world_add_sphere(VoxelObjectWorld *world, Vec3 position,
                                      float radius, uint8_t material)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;

    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float r_voxels = radius / obj->voxel_size;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = (float)x - half_grid + 0.5f;
                float dy = (float)y - half_grid + 0.5f;
                float dz = (float)z - half_grid + 0.5f;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);

                int32_t idx = vobj_index(x, y, z);
                if (dist <= r_voxels)
                {
                    obj->voxels[idx].material = material;
                    obj->voxel_count++;
                }
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

int32_t voxel_object_world_add_box(VoxelObjectWorld *world, Vec3 position,
                                   Vec3 half_extents, uint8_t material)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;

    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;

                int32_t idx = vobj_index(x, y, z);
                if (fabsf(dx) <= half_extents.x &&
                    fabsf(dy) <= half_extents.y &&
                    fabsf(dz) <= half_extents.z)
                {
                    obj->voxels[idx].material = material;
                    obj->voxel_count++;
                }
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

int32_t voxel_object_world_add_from_voxels(VoxelObjectWorld *world,
                                           const uint8_t *voxels,
                                           int32_t size_x, int32_t size_y, int32_t size_z,
                                           Vec3 origin, float voxel_size)
{
    if (!world || !voxels || size_x <= 0 || size_y <= 0 || size_z <= 0)
        return -1;

    if (size_x > VOBJ_GRID_SIZE || size_y > VOBJ_GRID_SIZE || size_z > VOBJ_GRID_SIZE)
        return -1;

    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->voxel_size = voxel_size;
    obj->voxel_count = 0;
    obj->active = true;
    obj->orientation = quat_identity();

    int32_t offset_x = (VOBJ_GRID_SIZE - size_x) / 2;
    int32_t offset_y = (VOBJ_GRID_SIZE - size_y) / 2;
    int32_t offset_z = (VOBJ_GRID_SIZE - size_z) / 2;

    for (int32_t z = 0; z < size_z; z++)
    {
        for (int32_t y = 0; y < size_y; y++)
        {
            for (int32_t x = 0; x < size_x; x++)
            {
                int32_t src_idx = x + y * size_x + z * size_x * size_y;
                uint8_t mat = voxels[src_idx];
                if (mat == 0)
                    continue;

                int32_t dst_x = x + offset_x;
                int32_t dst_y = y + offset_y;
                int32_t dst_z = z + offset_z;
                int32_t dst_idx = vobj_index(dst_x, dst_y, dst_z);

                obj->voxels[dst_idx].material = mat;
                obj->voxel_count++;
            }
        }
    }

    if (obj->voxel_count == 0)
    {
        obj->active = false;
        return -1;
    }

    float src_center_x = origin.x + (float)size_x * voxel_size * 0.5f;
    float src_center_y = origin.y + (float)size_y * voxel_size * 0.5f;
    float src_center_z = origin.z + (float)size_z * voxel_size * 0.5f;

    obj->position = vec3_create(src_center_x, src_center_y, src_center_z);

    voxel_object_recalc_shape(obj);
    return slot;
}

VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld *world, Vec3 origin, Vec3 dir)
{
    PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);

    VoxelObjectHit result = {0};
    result.hit = false;
    result.object_index = -1;

    float closest_t = 1e30f;

    for (int32_t i = 0; i < world->object_count; i++)
    {
        VoxelObject *obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0)
            continue;

        Vec3 pivot = obj->position;
        Vec3 oc = vec3_sub(origin, pivot);
        float a = vec3_dot(dir, dir);
        float b = 2.0f * vec3_dot(oc, dir);
        float c = vec3_dot(oc, oc) - obj->radius * obj->radius;
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant < 0.0f)
            continue;

        float sqrt_disc = sqrtf(discriminant);
        float t0 = (-b - sqrt_disc) / (2.0f * a);
        float t1 = (-b + sqrt_disc) / (2.0f * a);

        float t_sphere = t0;
        if (t_sphere < 0.0f)
            t_sphere = t1;
        if (c <= 0.0f)
            t_sphere = 0.0f;
        if (t_sphere < 0.0f || t_sphere >= closest_t)
            continue;

        float rot_mat[9], inv_rot_mat[9];
        quat_to_mat3(obj->orientation, rot_mat);
        mat3_transpose(rot_mat, inv_rot_mat);

        Vec3 local_origin = mat3_transform_vec3(inv_rot_mat, vec3_sub(origin, pivot));
        Vec3 local_dir = mat3_transform_vec3(inv_rot_mat, dir);

        float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
        local_origin = vec3_add(local_origin, vec3_create(half_size, half_size, half_size));

        Vec3 inv_dir;
        inv_dir.x = (fabsf(local_dir.x) > 0.0001f) ? 1.0f / local_dir.x : 1e10f;
        inv_dir.y = (fabsf(local_dir.y) > 0.0001f) ? 1.0f / local_dir.y : 1e10f;
        inv_dir.z = (fabsf(local_dir.z) > 0.0001f) ? 1.0f / local_dir.z : 1e10f;

        float t_start = fmaxf(t_sphere - obj->radius * 0.2f, 0.0f);
        Vec3 pos = vec3_add(local_origin, vec3_scale(local_dir, t_start));

        int32_t map_x = (int32_t)floorf(pos.x / obj->voxel_size);
        int32_t map_y = (int32_t)floorf(pos.y / obj->voxel_size);
        int32_t map_z = (int32_t)floorf(pos.z / obj->voxel_size);

        int32_t step_x = (local_dir.x >= 0.0f) ? 1 : -1;
        int32_t step_y = (local_dir.y >= 0.0f) ? 1 : -1;
        int32_t step_z = (local_dir.z >= 0.0f) ? 1 : -1;

        float t_max_x = ((float)(map_x + (step_x > 0 ? 1 : 0)) * obj->voxel_size - pos.x) * inv_dir.x;
        float t_max_y = ((float)(map_y + (step_y > 0 ? 1 : 0)) * obj->voxel_size - pos.y) * inv_dir.y;
        float t_max_z = ((float)(map_z + (step_z > 0 ? 1 : 0)) * obj->voxel_size - pos.z) * inv_dir.z;

        float t_delta_x = fabsf(obj->voxel_size * inv_dir.x);
        float t_delta_y = fabsf(obj->voxel_size * inv_dir.y);
        float t_delta_z = fabsf(obj->voxel_size * inv_dir.z);

        float t_current = t_start;
        Vec3 hit_normal = vec3_zero();

        for (int32_t step = 0; step < VOBJ_GRID_SIZE * 6; step++)
        {
            if (map_x >= 0 && map_x < VOBJ_GRID_SIZE &&
                map_y >= 0 && map_y < VOBJ_GRID_SIZE &&
                map_z >= 0 && map_z < VOBJ_GRID_SIZE)
            {
                int32_t idx = vobj_index(map_x, map_y, map_z);
                if (obj->voxels[idx].material != 0)
                {
                    if (t_current < closest_t)
                    {
                        closest_t = t_current;
                        result.hit = true;
                        result.object_index = i;
                        result.impact_point = vec3_add(origin, vec3_scale(dir, t_current));
                        result.impact_normal = mat3_transform_vec3(rot_mat, hit_normal);
                        result.impact_normal_local = hit_normal;
                        result.voxel_x = map_x;
                        result.voxel_y = map_y;
                        result.voxel_z = map_z;
                    }
                    break;
                }
            }

            if (t_max_x < t_max_y && t_max_x < t_max_z)
            {
                t_current = t_start + t_max_x;
                t_max_x += t_delta_x;
                map_x += step_x;
                hit_normal = vec3_create((float)(-step_x), 0.0f, 0.0f);
            }
            else if (t_max_y < t_max_z)
            {
                t_current = t_start + t_max_y;
                t_max_y += t_delta_y;
                map_y += step_y;
                hit_normal = vec3_create(0.0f, (float)(-step_y), 0.0f);
            }
            else
            {
                t_current = t_start + t_max_z;
                t_max_z += t_delta_z;
                map_z += step_z;
                hit_normal = vec3_create(0.0f, 0.0f, (float)(-step_z));
            }

            if (t_current > closest_t)
                break;
        }
    }

    PROFILE_END(PROFILE_VOXEL_RAYCAST);
    return result;
}

void voxel_object_world_queue_split(VoxelObjectWorld *world, int32_t obj_index)
{
    if (!world || obj_index < 0 || obj_index >= world->object_count)
        return;

    int32_t next_tail = (world->split_queue_tail + 1) % VOBJ_SPLIT_QUEUE_SIZE;
    if (next_tail == world->split_queue_head)
        return; /* Queue full */

    world->split_queue[world->split_queue_tail] = obj_index;
    world->split_queue_tail = next_tail;
}

void voxel_object_world_process_recalcs(VoxelObjectWorld *world)
{
    if (!world)
        return;

    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    int32_t processed = 0;
    for (int32_t i = 0; i < world->object_count && processed < VOBJ_MAX_RECALCS_PER_TICK; i++)
    {
        VoxelObject *obj = &world->objects[i];
        if (obj->active && obj->shape_dirty)
        {
            voxel_object_recalc_shape(obj);
            processed++;
        }
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
}

static void flood_fill_voxels_local(const VoxelObject *obj, uint8_t *visited,
                                     int32_t start_x, int32_t start_y, int32_t start_z)
{
    static int32_t stack[VOBJ_TOTAL_VOXELS];
    int32_t stack_top = 0;

    int32_t start_idx = vobj_index(start_x, start_y, start_z);
    if (start_x < 0 || start_x >= VOBJ_GRID_SIZE ||
        start_y < 0 || start_y >= VOBJ_GRID_SIZE ||
        start_z < 0 || start_z >= VOBJ_GRID_SIZE ||
        visited[start_idx] || obj->voxels[start_idx].material == 0)
        return;

    stack[stack_top++] = start_idx;
    visited[start_idx] = 1;

    static const int32_t dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, -1, 1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, -1, 1};

    while (stack_top > 0)
    {
        int32_t idx = stack[--stack_top];
        int32_t x, y, z;
        vobj_coords(idx, &x, &y, &z);

        for (int32_t i = 0; i < 6; i++)
        {
            int32_t nx = x + dx[i];
            int32_t ny = y + dy[i];
            int32_t nz = z + dz[i];

            if (nx < 0 || nx >= VOBJ_GRID_SIZE ||
                ny < 0 || ny >= VOBJ_GRID_SIZE ||
                nz < 0 || nz >= VOBJ_GRID_SIZE)
                continue;

            int32_t nidx = vobj_index(nx, ny, nz);
            if (visited[nidx] || obj->voxels[nidx].material == 0)
                continue;

            visited[nidx] = 1;
            stack[stack_top++] = nidx;
        }
    }
}

static bool split_one_island(VoxelObjectWorld *world, int32_t obj_index)
{
    if (obj_index < 0 || obj_index >= world->object_count)
        return false;

    VoxelObject *obj = &world->objects[obj_index];
    if (!obj->active || obj->voxel_count <= 1)
        return false;

    uint8_t visited[VOBJ_TOTAL_VOXELS] = {0};

    int32_t first_x = -1, first_y = -1, first_z = -1;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS && first_x < 0; i++)
    {
        if (obj->voxels[i].material != 0)
        {
            vobj_coords(i, &first_x, &first_y, &first_z);
        }
    }
    if (first_x < 0)
        return false;

    flood_fill_voxels_local(obj, visited, first_x, first_y, first_z);

    int32_t unvisited_count = 0;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material != 0 && !visited[i])
        {
            unvisited_count++;
        }
    }
    if (unvisited_count == 0)
        return false;

    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return false;

    VoxelObject *new_obj = &world->objects[world->object_count];
    memset(new_obj, 0, sizeof(VoxelObject));
    new_obj->position = obj->position;
    new_obj->orientation = obj->orientation;
    new_obj->voxel_size = obj->voxel_size;
    new_obj->active = true;
    new_obj->shape_dirty = true;
    new_obj->voxel_count = 0;

    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material != 0 && !visited[i])
        {
            new_obj->voxels[i].material = obj->voxels[i].material;
            new_obj->voxel_count++;
            obj->voxels[i].material = 0;
            obj->voxel_count--;
        }
    }

    int32_t new_obj_idx = world->object_count;
    world->object_count++;

    obj->shape_dirty = true;

    /* Queue both for further splitting */
    voxel_object_world_queue_split(world, obj_index);
    voxel_object_world_queue_split(world, new_obj_idx);

    return true;
}

void voxel_object_world_process_splits(VoxelObjectWorld *world)
{
    if (!world)
        return;

    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    int32_t processed = 0;
    while (world->split_queue_head != world->split_queue_tail &&
           processed < VOBJ_MAX_SPLITS_PER_TICK)
    {
        int32_t obj_index = world->split_queue[world->split_queue_head];
        world->split_queue_head = (world->split_queue_head + 1) % VOBJ_SPLIT_QUEUE_SIZE;

        if (split_one_island(world, obj_index))
        {
            processed++;
        }
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
}
