#include "detach.h"
#include "engine/core/profile.h"
#include <string.h>
#include <math.h>

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
        /* Defer shape recalc and island splitting to per-frame budget */
        voxel_object_world_mark_dirty(world, obj_index);
        voxel_object_world_queue_split(world, obj_index);
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

    /* Always use dirty-only path to avoid full volume scan (500K+ ops risk) */
    if (vol->last_edit_count > 0)
    {
        connectivity_analyze_dirty(vol, anchor_y, 0, work, &conn_result);
    }
    else
    {
        /* No edits - skip connectivity analysis entirely */
        memset(&conn_result, 0, sizeof(ConnectivityResult));
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
