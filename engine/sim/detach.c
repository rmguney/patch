#include "detach.h"
#include "engine/core/profile.h"
#include <string.h>
#include <math.h>

static void flood_fill_voxels(const VoxelObject *obj, uint8_t *visited,
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

static void split_disconnected_islands(VoxelObjectWorld *world, int32_t obj_index)
{
    static int32_t work_queue[VOBJ_MAX_OBJECTS];
    int32_t queue_head = 0;
    int32_t queue_tail = 0;

    work_queue[queue_tail++] = obj_index;

    while (queue_head < queue_tail)
    {
        int32_t current_idx = work_queue[queue_head++];
        VoxelObject *obj = &world->objects[current_idx];

        if (!obj->active || obj->voxel_count <= 1)
            continue;

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
            continue;

        flood_fill_voxels(obj, visited, first_x, first_y, first_z);

        int32_t unvisited_count = 0;
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            if (obj->voxels[i].material != 0 && !visited[i])
            {
                unvisited_count++;
            }
        }
        if (unvisited_count == 0)
            continue;

        if (world->object_count >= VOBJ_MAX_OBJECTS)
            continue;

        VoxelObject *new_obj = &world->objects[world->object_count];
        memset(new_obj, 0, sizeof(VoxelObject));
        new_obj->position = obj->position;
        new_obj->orientation = obj->orientation;
        new_obj->voxel_size = obj->voxel_size;
        new_obj->active = true;
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

        voxel_object_recalc_shape(obj);
        voxel_object_recalc_shape(new_obj);

        if (queue_tail < VOBJ_MAX_OBJECTS)
            work_queue[queue_tail++] = current_idx;
        if (queue_tail < VOBJ_MAX_OBJECTS)
            work_queue[queue_tail++] = new_obj_idx;
    }
}

int32_t detach_object_at_point(VoxelObjectWorld *world, int32_t obj_index,
                               Vec3 impact_point, float destroy_radius,
                               Vec3 *out_positions, uint8_t *out_materials,
                               int32_t max_output)
{
    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    if (obj_index < 0 || obj_index >= world->object_count)
    {
        PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
        return 0;
    }

    VoxelObject *obj = &world->objects[obj_index];
    if (!obj->active)
    {
        PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
        return 0;
    }

    int32_t destroyed_count = 0;
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    float rot_mat[9];
    quat_to_mat3(obj->orientation, rot_mat);
    Vec3 pivot = obj->position;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                if (obj->voxels[idx].material == 0)
                    continue;

                Vec3 local_pos;
                local_pos.x = ((float)x + 0.5f) * obj->voxel_size - half_size;
                local_pos.y = ((float)y + 0.5f) * obj->voxel_size - half_size;
                local_pos.z = ((float)z + 0.5f) * obj->voxel_size - half_size;

                Vec3 rotated = mat3_transform_vec3(rot_mat, local_pos);
                Vec3 voxel_pos = vec3_add(pivot, rotated);

                float dist = vec3_length(vec3_sub(voxel_pos, impact_point));

                if (dist < destroy_radius)
                {
                    if (destroyed_count < max_output)
                    {
                        if (out_positions)
                            out_positions[destroyed_count] = voxel_pos;
                        if (out_materials)
                            out_materials[destroyed_count] = obj->voxels[idx].material;
                    }

                    obj->voxels[idx].material = 0;
                    obj->voxel_count--;
                    destroyed_count++;
                }
            }
        }
    }

    if (obj->voxel_count <= 0)
    {
        obj->active = false;
    }
    else
    {
        voxel_object_recalc_shape(obj);
        split_disconnected_islands(world, obj_index);
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
    return destroyed_count;
}

void detach_terrain_process(VoxelVolume *vol,
                            VoxelObjectWorld *obj_world,
                            const DetachConfig *config,
                            ConnectivityWorkBuffer *work,
                            DetachResult *result)
{
    DetachResult local_result = {0};

    if (!vol || !obj_world || !config || !work || !config->enabled)
    {
        if (result)
            *result = local_result;
        return;
    }

    int32_t active_bodies = 0;
    for (int32_t i = 0; i < obj_world->object_count; i++)
    {
        if (obj_world->objects[i].active)
            active_bodies++;
    }

    float anchor_y = vol->bounds.min_y + config->anchor_y_offset;
    ConnectivityResult conn_result;
    if (vol->last_edit_count > 0)
    {
        connectivity_analyze_dirty(vol, anchor_y, 0, work, &conn_result);
    }
    else
    {
        connectivity_analyze_volume(vol, anchor_y, 0, work, &conn_result);
    }

    int32_t processed = 0;
    for (int32_t i = 0; i < conn_result.island_count && processed < config->max_islands_per_tick; i++)
    {
        IslandInfo *island = &conn_result.islands[i];
        if (!island->is_floating)
            continue;

        local_result.islands_processed++;

        if (island->voxel_count < config->min_voxels_per_island)
        {
            connectivity_remove_island(vol, island, work);
            local_result.voxels_removed += island->voxel_count;
            processed++;
            continue;
        }

        if (island->voxel_count > config->max_voxels_per_island)
        {
            local_result.islands_skipped++;
            continue;
        }

        if (active_bodies >= config->max_bodies_alive)
        {
            local_result.islands_skipped++;
            continue;
        }

        int32_t ext_size_x = island->voxel_max_x - island->voxel_min_x + 1;
        int32_t ext_size_y = island->voxel_max_y - island->voxel_min_y + 1;
        int32_t ext_size_z = island->voxel_max_z - island->voxel_min_z + 1;

        if (ext_size_x > VOBJ_GRID_SIZE || ext_size_y > VOBJ_GRID_SIZE || ext_size_z > VOBJ_GRID_SIZE)
        {
            local_result.islands_skipped++;
            continue;
        }

        uint8_t extract_buf[VOBJ_TOTAL_VOXELS];
        memset(extract_buf, 0, sizeof(extract_buf));

        Vec3 extract_origin;
        int32_t extracted = connectivity_extract_island_with_ids(vol, island, work,
                                                                 extract_buf,
                                                                 ext_size_x, ext_size_y, ext_size_z,
                                                                 &extract_origin);

        if (extracted <= 0)
            continue;

        int32_t obj_idx = voxel_object_world_add_from_voxels(
            obj_world, extract_buf,
            ext_size_x, ext_size_y, ext_size_z,
            extract_origin, vol->voxel_size);

        if (obj_idx >= 0)
        {
            connectivity_remove_island(vol, island, work);
            local_result.bodies_spawned++;
            active_bodies++;
        }

        processed++;
    }

    if (result)
        *result = local_result;
}
