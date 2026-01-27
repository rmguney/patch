#include "detach.h"
#include "engine/core/profile.h"
#include <string.h>

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
                        destroyed_count++;
                    }

                    obj->voxels[idx].material = 0;
                    obj->voxel_count--;
                }
            }
        }
    }

    if (obj->voxel_count <= 0)
    {
        obj->active = false;
        voxel_object_world_free_slot(world, obj_index);
    }
    else
    {
        /* Defer shape recalc and island splitting to per-frame budget */
        voxel_object_world_mark_dirty(world, obj_index);
        voxel_object_world_queue_split(world, obj_index);
    }

    if (destroyed_count > 0)
    {
        obj->voxel_revision++;
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

    /* Only analyze the region around recently edited chunks to avoid
     * scanning the entire volume (which is O(total_voxels) per tick
     * and can cause false fragmentation via BFS stack overflow). */
    connectivity_analyze_dirty(vol, anchor_y, 0, work, &conn_result);

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


        if (active_bodies >= config->max_bodies_alive)
        {
            local_result.islands_skipped++;
            continue;
        }

        int32_t ext_size_x = island->voxel_max_x - island->voxel_min_x + 1;
        int32_t ext_size_y = island->voxel_max_y - island->voxel_min_y + 1;
        int32_t ext_size_z = island->voxel_max_z - island->voxel_min_z + 1;

        /* Oversized islands: BFS-based subdivision into organic 32³ chunks */
        if (ext_size_x > VOBJ_GRID_SIZE || ext_size_y > VOBJ_GRID_SIZE || ext_size_z > VOBJ_GRID_SIZE)
        {
            uint8_t target_id = (uint8_t)island->island_id;
            bool any_spawned = false;
            int32_t chunks_x = vol->chunks_x;
            int32_t chunks_xy = vol->chunks_x * vol->chunks_y;

            /* Use a fresh visited generation for consumed-voxel tracking */
            work->generation++;
            if (work->generation == 0)
            {
                work->generation = 1;
                memset(work->visited_gen, 0, (size_t)work->visited_size);
            }
            uint8_t consumed_gen = work->generation;

            /* Scan island for seed voxels, BFS each into a bounded sub-group */
            for (int32_t seed_z = island->voxel_min_z; seed_z <= island->voxel_max_z; seed_z++)
            {
                for (int32_t seed_y = island->voxel_min_y; seed_y <= island->voxel_max_y; seed_y++)
                {
                    for (int32_t seed_x = island->voxel_min_x; seed_x <= island->voxel_max_x; seed_x++)
                    {
                        if (active_bodies >= config->max_bodies_alive)
                            goto oversized_done;

                        int32_t scx = seed_x / CHUNK_SIZE, slx = seed_x % CHUNK_SIZE;
                        int32_t scy = seed_y / CHUNK_SIZE, sly = seed_y % CHUNK_SIZE;
                        int32_t scz = seed_z / CHUNK_SIZE, slz = seed_z % CHUNK_SIZE;
                        int32_t seed_gi = (scx + scy * chunks_x + scz * chunks_xy) * CHUNK_VOXEL_COUNT +
                                           slx + sly * CHUNK_SIZE + slz * CHUNK_SIZE * CHUNK_SIZE;
                        if (seed_gi < 0 || seed_gi >= work->island_ids_size)
                            continue;
                        if (work->island_ids[seed_gi] != target_id)
                            continue;
                        if (work->visited_gen[seed_gi] == consumed_gen)
                            continue;

                        /* Use a unique generation for this group so we can extract it */
                        work->generation++;
                        if (work->generation == 0)
                        {
                            work->generation = 1;
                            memset(work->visited_gen, 0, (size_t)work->visited_size);
                        }
                        uint8_t group_gen = work->generation;

                        int32_t gmin_x = seed_x, gmax_x = seed_x;
                        int32_t gmin_y = seed_y, gmax_y = seed_y;
                        int32_t gmin_z = seed_z, gmax_z = seed_z;

                        /* BFS with 32³ bounding box constraint */
                        work->visited_gen[seed_gi] = group_gen;
                        work->stack[0] = seed_gi;
                        int32_t front = 0, back = 1;

                        static const int32_t dx[6] = {1,-1, 0, 0, 0, 0};
                        static const int32_t dy[6] = {0, 0, 1,-1, 0, 0};
                        static const int32_t dz[6] = {0, 0, 0, 0, 1,-1};

                        while (front < back)
                        {
                            int32_t gi = work->stack[front++];
                            int32_t li = gi % CHUNK_VOXEL_COUNT;
                            int32_t ci = gi / CHUNK_VOXEL_COUNT;
                            int32_t vx = (ci % chunks_x) * CHUNK_SIZE + (li % CHUNK_SIZE);
                            int32_t vy = ((ci / chunks_x) % vol->chunks_y) * CHUNK_SIZE +
                                         ((li / CHUNK_SIZE) % CHUNK_SIZE);
                            int32_t vz = (ci / chunks_xy) * CHUNK_SIZE +
                                         (li / (CHUNK_SIZE * CHUNK_SIZE));

                            for (int32_t d = 0; d < 6; d++)
                            {
                                int32_t nx = vx + dx[d];
                                int32_t ny = vy + dy[d];
                                int32_t nz = vz + dz[d];
                                if (nx < island->voxel_min_x || nx > island->voxel_max_x ||
                                    ny < island->voxel_min_y || ny > island->voxel_max_y ||
                                    nz < island->voxel_min_z || nz > island->voxel_max_z)
                                    continue;

                                /* Check 32³ constraint before adding */
                                int32_t new_min_x = gmin_x < nx ? gmin_x : nx;
                                int32_t new_max_x = gmax_x > nx ? gmax_x : nx;
                                int32_t new_min_y = gmin_y < ny ? gmin_y : ny;
                                int32_t new_max_y = gmax_y > ny ? gmax_y : ny;
                                int32_t new_min_z = gmin_z < nz ? gmin_z : nz;
                                int32_t new_max_z = gmax_z > nz ? gmax_z : nz;
                                if (new_max_x - new_min_x + 1 > VOBJ_GRID_SIZE ||
                                    new_max_y - new_min_y + 1 > VOBJ_GRID_SIZE ||
                                    new_max_z - new_min_z + 1 > VOBJ_GRID_SIZE)
                                    continue;

                                int32_t ncx = nx / CHUNK_SIZE, nlx = nx % CHUNK_SIZE;
                                int32_t ncy = ny / CHUNK_SIZE, nly = ny % CHUNK_SIZE;
                                int32_t ncz = nz / CHUNK_SIZE, nlz = nz % CHUNK_SIZE;
                                int32_t ngi = (ncx + ncy * chunks_x + ncz * chunks_xy) * CHUNK_VOXEL_COUNT +
                                               nlx + nly * CHUNK_SIZE + nlz * CHUNK_SIZE * CHUNK_SIZE;
                                if (ngi < 0 || ngi >= work->island_ids_size)
                                    continue;
                                if (work->island_ids[ngi] != target_id)
                                    continue;
                                if (work->visited_gen[ngi] == group_gen ||
                                    work->visited_gen[ngi] == consumed_gen)
                                    continue;

                                gmin_x = new_min_x; gmax_x = new_max_x;
                                gmin_y = new_min_y; gmax_y = new_max_y;
                                gmin_z = new_min_z; gmax_z = new_max_z;

                                work->visited_gen[ngi] = group_gen;
                                if (back < work->stack_capacity)
                                    work->stack[back++] = ngi;
                            }
                        }

                        /* Extract group voxels by scanning the bounded region */
                        int32_t sub_ex = gmax_x - gmin_x + 1;
                        int32_t sub_ey = gmax_y - gmin_y + 1;
                        int32_t sub_ez = gmax_z - gmin_z + 1;

                        uint8_t sub_buf[VOBJ_TOTAL_VOXELS];
                        memset(sub_buf, 0, sizeof(sub_buf));
                        int32_t sub_count = 0;

                        for (int32_t gz = gmin_z; gz <= gmax_z; gz++)
                        {
                            for (int32_t gy = gmin_y; gy <= gmax_y; gy++)
                            {
                                for (int32_t gx = gmin_x; gx <= gmax_x; gx++)
                                {
                                    int32_t cx = gx / CHUNK_SIZE, lx = gx % CHUNK_SIZE;
                                    int32_t cy = gy / CHUNK_SIZE, ly = gy % CHUNK_SIZE;
                                    int32_t cz = gz / CHUNK_SIZE, lz = gz % CHUNK_SIZE;
                                    int32_t gi = (cx + cy * chunks_x + cz * chunks_xy) * CHUNK_VOXEL_COUNT +
                                                  lx + ly * CHUNK_SIZE + lz * CHUNK_SIZE * CHUNK_SIZE;
                                    if (gi < 0 || gi >= work->island_ids_size)
                                        continue;
                                    if (work->visited_gen[gi] != group_gen)
                                        continue;

                                    Chunk *chunk = volume_get_chunk(vol, cx, cy, cz);
                                    if (!chunk)
                                        continue;
                                    uint8_t mat = chunk_get(chunk, lx, ly, lz);
                                    if (mat == 0)
                                        continue;

                                    int32_t ox = gx - gmin_x;
                                    int32_t oy = gy - gmin_y;
                                    int32_t oz = gz - gmin_z;
                                    sub_buf[ox + oy * sub_ex + oz * sub_ex * sub_ey] = mat;
                                    sub_count++;

                                    /* Mark as consumed so future seeds skip these */
                                    work->visited_gen[gi] = consumed_gen;
                                }
                            }
                        }

                        if (sub_count == 0)
                            continue;

                        Vec3 sub_origin = vec3_create(
                            vol->bounds.min_x + gmin_x * vol->voxel_size,
                            vol->bounds.min_y + gmin_y * vol->voxel_size,
                            vol->bounds.min_z + gmin_z * vol->voxel_size);

                        int32_t obj_idx = voxel_object_world_add_from_voxels(
                            obj_world, sub_buf,
                            sub_ex, sub_ey, sub_ez,
                            sub_origin, vol->voxel_size);

                        if (obj_idx >= 0)
                        {
                            any_spawned = true;
                            obj_world->objects[obj_idx].render_delay = 3;
                            if (local_result.bodies_spawned < DETACH_MAX_SPAWNED)
                                local_result.spawned_indices[local_result.bodies_spawned] = obj_idx;
                            local_result.bodies_spawned++;
                            active_bodies++;
                        }
                    }
                }
            }

        oversized_done:
            connectivity_remove_island(vol, island, work);
            if (!any_spawned)
                local_result.voxels_removed += island->voxel_count;
            processed++;
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
        {
            processed++;
            continue;
        }

        int32_t obj_idx = voxel_object_world_add_from_voxels(
            obj_world, extract_buf,
            ext_size_x, ext_size_y, ext_size_z,
            extract_origin, vol->voxel_size);

        if (obj_idx >= 0)
        {
            connectivity_remove_island(vol, island, work);

            /* Delay rendering until terrain GPU chunks sync (avoids overlap artifacts) */
            obj_world->objects[obj_idx].render_delay = 3;

            if (local_result.bodies_spawned < DETACH_MAX_SPAWNED)
                local_result.spawned_indices[local_result.bodies_spawned] = obj_idx;
            local_result.bodies_spawned++;
            active_bodies++;
        }

        processed++;
    }

    if (result)
        *result = local_result;
}
