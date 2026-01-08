#include "engine/sim/terrain_detach.h"
#include <string.h>

void terrain_detach_process(VoxelVolume *vol,
                            VoxelObjectWorld *obj_world,
                            const TerrainDetachConfig *config,
                            ConnectivityWorkBuffer *work,
                            Vec3 impact_point,
                            RngState *rng,
                            TerrainDetachResult *result)
{
    TerrainDetachResult local_result = {0};

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

    /* Run connectivity analysis on dirty regions */
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

    /* Process floating islands */
    int32_t processed = 0;
    for (int32_t i = 0; i < conn_result.island_count && processed < config->max_islands_per_tick; i++)
    {
        IslandInfo *island = &conn_result.islands[i];
        if (!island->is_floating)
            continue;

        local_result.islands_processed++;

        /* Check size thresholds */
        if (island->voxel_count < config->min_voxels_per_island)
        {
            /* Too small: just remove from volume (could spawn particles here) */
            connectivity_remove_island(vol, island, work);
            local_result.voxels_removed += island->voxel_count;
            processed++;
            continue;
        }

        if (island->voxel_count > config->max_voxels_per_island)
        {
            /* Too large: leave in terrain */
            local_result.islands_skipped++;
            continue;
        }

        /* Check body capacity */
        if (active_bodies >= config->max_bodies_alive)
        {
            local_result.islands_skipped++;
            continue;
        }

        /* Extract island voxels */
        int32_t ext_size_x = island->voxel_max_x - island->voxel_min_x + 1;
        int32_t ext_size_y = island->voxel_max_y - island->voxel_min_y + 1;
        int32_t ext_size_z = island->voxel_max_z - island->voxel_min_z + 1;

        /* Skip if too large for object grid */
        if (ext_size_x > VOBJ_GRID_SIZE || ext_size_y > VOBJ_GRID_SIZE || ext_size_z > VOBJ_GRID_SIZE)
        {
            local_result.islands_skipped++;
            continue;
        }

        /* Use stack-allocated buffer for extraction (bounded size) */
        uint8_t extract_buf[VOBJ_TOTAL_VOXELS];
        memset(extract_buf, 0, sizeof(extract_buf));

        Vec3 extract_origin;
        int32_t extracted = connectivity_extract_island_with_ids(vol, island, work,
                                                                 extract_buf,
                                                                 ext_size_x, ext_size_y, ext_size_z,
                                                                 &extract_origin);

        if (extracted <= 0)
            continue;

        /* Compute initial velocity from impact point */
        Vec3 island_center = island->center_of_mass;
        Vec3 dir = vec3_sub(island_center, impact_point);
        float dist = vec3_length(dir);
        if (dist > 0.001f)
        {
            dir = vec3_scale(dir, 1.0f / dist);
        }
        else
        {
            dir = vec3_create(0.0f, 1.0f, 0.0f);
        }

        Vec3 initial_vel = vec3_create(
            dir.x * config->initial_impulse_scale.x + rng_range_f32(rng, -0.5f, 0.5f),
            dir.y * config->initial_impulse_scale.y + rng_range_f32(rng, 0.0f, 1.0f),
            dir.z * config->initial_impulse_scale.z + rng_range_f32(rng, -0.5f, 0.5f));

        /* Spawn voxel object */
        int32_t obj_idx = voxel_object_world_add_from_voxels(
            obj_world, extract_buf,
            ext_size_x, ext_size_y, ext_size_z,
            extract_origin, vol->voxel_size,
            initial_vel, rng);

        if (obj_idx >= 0)
        {
            /* Remove island from terrain */
            connectivity_remove_island(vol, island, work);
            local_result.bodies_spawned++;
            active_bodies++;
        }

        processed++;
    }

    if (result)
        *result = local_result;
}
