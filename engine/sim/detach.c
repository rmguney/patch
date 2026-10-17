#include "detach.h"
#include "content/materials.h"
#include "engine/core/profile.h"
#include <math.h>
#include <string.h>

/* The oversized-island split uses the connectivity stack as a BFS queue (of packed voxel
 * positions) for one group of at most VOBJ_TOTAL_VOXELS voxels */
STATIC_ASSERT(CONNECTIVITY_MIN_STACK_SIZE >= VOBJ_TOTAL_VOXELS, "split queue must fit the work stack");

/* The cube around the largest terrain destruction sphere bounds its edits */
STATIC_ASSERT((2 * DETACH_TERRAIN_MAX_RADIUS_VOXELS + 1) * (2 * DETACH_TERRAIN_MAX_RADIUS_VOXELS + 1) *
                      (2 * DETACH_TERRAIN_MAX_RADIUS_VOXELS + 1) <= VOLUME_MAX_EDITS_PER_TICK,
              "terrain destruction sphere must fit one edit batch");

int32_t detach_object_at_point(VoxelObjectWorld *world, int32_t obj_index,
                               Vec3 impact_point, float destroy_radius,
                               Vec3 *out_positions, uint8_t *out_materials,
                               int32_t max_output)
{
    if (obj_index < 0 || obj_index >= world->object_count)
        return 0;

    VoxelObject *obj = &world->objects[obj_index];
    if (!obj->active)
        return 0;

    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    /* Work in the object's grid space: rotate the impact point in once instead of
     * every voxel out, and visit only the grid cells the sphere can reach */
    float rot_mat[9], inv_rot_mat[9];
    quat_to_mat3(obj->orientation, rot_mat);
    mat3_transpose(rot_mat, inv_rot_mat);

    float vs = obj->voxel_size;
    float half_size = vs * (float)VOBJ_GRID_SIZE * 0.5f;
    Vec3 local_impact = mat3_transform_vec3(inv_rot_mat, vec3_sub(impact_point, obj->position));
    float radius_sq = destroy_radius * destroy_radius;

    float impact_grid[3] = {(local_impact.x + half_size) / vs,
                            (local_impact.y + half_size) / vs,
                            (local_impact.z + half_size) / vs};
    float radius_grid = destroy_radius / vs;
    int32_t lo[3], hi[3];
    for (int32_t a = 0; a < 3; a++)
    {
        lo[a] = (int32_t)fmaxf(floorf(impact_grid[a] - radius_grid), 0.0f);
        hi[a] = (int32_t)fminf(floorf(impact_grid[a] + radius_grid), (float)(VOBJ_GRID_SIZE - 1));
    }

    int32_t destroyed_count = 0;
    uint64_t changed_bricks = 0;
    for (int32_t z = lo[2]; z <= hi[2]; z++)
    {
        for (int32_t y = lo[1]; y <= hi[1]; y++)
        {
            for (int32_t x = lo[0]; x <= hi[0]; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                if (obj->voxels[idx].material == 0)
                    continue;

                Vec3 local_pos = vec3_create(((float)x + 0.5f) * vs - half_size,
                                             ((float)y + 0.5f) * vs - half_size,
                                             ((float)z + 0.5f) * vs - half_size);
                if (vec3_length_sq(vec3_sub(local_pos, local_impact)) >= radius_sq)
                    continue;

                if (destroyed_count < max_output)
                {
                    if (out_positions)
                        out_positions[destroyed_count] =
                            vec3_add(obj->position, mat3_transform_vec3(rot_mat, local_pos));
                    if (out_materials)
                        out_materials[destroyed_count] = obj->voxels[idx].material;
                }
                destroyed_count++;

                obj->voxels[idx].material = 0;
                obj->voxel_count--;
                changed_bricks |= 1ull << vobj_brick_index(obj, x, y, z);
            }
        }
    }

    if (destroyed_count > 0)
    {
        voxel_object_mark_changed(obj, changed_bricks);
        if (obj->voxel_count <= 0)
        {
            voxel_object_world_free_slot(world, obj_index);
        }
        else
        {
            /* Defer shape recalc and island splitting to per-frame budget */
            voxel_object_world_mark_dirty(world, obj_index);
            voxel_object_world_queue_split(world, obj_index);
        }
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
    return destroyed_count;
}

int32_t detach_terrain_at_point(VoxelVolume *vol, Vec3 center, int32_t radius_voxels,
                                Vec3 *out_positions, uint8_t *out_materials,
                                int32_t max_output)
{
    const float vs = vol->voxel_size;
    const int32_t r = radius_voxels < DETACH_TERRAIN_MAX_RADIUS_VOXELS ? radius_voxels
                                                                     : DETACH_TERRAIN_MAX_RADIUS_VOXELS;
    int32_t output_count = 0;

    volume_edit_begin(vol);
    for (int32_t ix = -r; ix <= r; ix++)
    {
        for (int32_t iy = -r; iy <= r; iy++)
        {
            for (int32_t iz = -r; iz <= r; iz++)
            {
                if (ix * ix + iy * iy + iz * iz > r * r)
                    continue;

                Vec3 pos = vec3_add(center, vec3_scale(vec3_create((float)ix, (float)iy, (float)iz), vs));
                uint8_t mat = volume_get_at(vol, pos);
                if (!material_is_breakable(mat))
                    continue;

                volume_edit_set(vol, pos, MATERIAL_EMPTY);
                if (output_count < max_output)
                {
                    if (out_positions)
                        out_positions[output_count] = pos;
                    if (out_materials)
                        out_materials[output_count] = mat;
                    output_count++;
                }
            }
        }
    }
    return volume_edit_end(vol);
}

/* Material of the voxel at a volume_voxel_linear_index */
static inline uint8_t volume_voxel_material(const VoxelVolume *vol, size_t voxel_index)
{
    return vol->chunks[voxel_index / CHUNK_VOXEL_COUNT].voxels[voxel_index % CHUNK_VOXEL_COUNT].material;
}

static void detach_spawn_island_particles(const VoxelVolume *vol,
                                          const IslandInfo *island,
                                          const ConnectivityWorkBuffer *work,
                                          ParticleSystem *particles,
                                          RngState *rng)
{
    uint16_t target_id = (uint16_t)island->island_id;
    float vs = vol->voxel_size;

    for (int32_t z = island->voxel_min_z; z <= island->voxel_max_z; z++)
    {
        for (int32_t y = island->voxel_min_y; y <= island->voxel_max_y; y++)
        {
            for (int32_t x = island->voxel_min_x; x <= island->voxel_max_x; x++)
            {
                size_t gi = volume_voxel_linear_index(vol, x, y, z);
                if (work->island_ids[gi] != target_id)
                    continue;

                uint8_t mat = volume_voxel_material(vol, gi);
                if (mat == 0)
                    continue;

                Vec3 pos = vec3_create(
                    vol->bounds.min_x + ((float)x + 0.5f) * vs,
                    vol->bounds.min_y + ((float)y + 0.5f) * vs,
                    vol->bounds.min_z + ((float)z + 0.5f) * vs);

                Vec3 dir = vec3_sub(pos, island->center_of_mass);
                float dist = vec3_length(dir);
                if (dist > 0.001f)
                    dir = vec3_scale(dir, 1.0f / dist);
                else
                    dir = vec3_create(0.0f, 1.0f, 0.0f);

                float speed = 1.0f + rng_float(rng) * 1.5f;
                Vec3 velocity = vec3_scale(dir, speed);
                velocity.y += 0.5f;

                Vec3 color = material_get_color(mat);
                particle_system_add(particles, pos, velocity, color, vs * 0.4f);
            }
        }
    }
}

/* Next visited generation of the work buffer, resetting all stamps on wrap */
static uint16_t next_generation(ConnectivityWorkBuffer *work)
{
    work->generation++;
    if (work->generation == 0)
    {
        work->generation = 1;
        memset(work->visited_gen, 0, work->visited_size * sizeof(uint16_t));
    }
    return work->generation;
}

/* Spawn an island wider than VOBJ_GRID_SIZE as several bodies: grow a group from
 * each voxel not yet extracted (BFS within the island, bounding box capped at
 * VOBJ_GRID_SIZE) and turn every group into a body, up to max_bodies. Returns the
 * number of bodies spawned. */
static int32_t detach_split_oversized_island(const VoxelVolume *vol, VoxelObjectWorld *obj_world,
                                             const IslandInfo *island, ConnectivityWorkBuffer *work,
                                             int32_t max_bodies, DetachResult *result)
{
    /* Two stamps for the whole island: members of the growing group carry group_gen
     * until the group is extracted, then consumed_gen. Taking both up front (not a
     * fresh one per group) keeps a generation wrap, and the stamp reset it causes,
     * from erasing the consumed marks halfway through. */
    uint16_t consumed_gen = next_generation(work);
    uint16_t group_gen = next_generation(work);

    uint16_t target_id = (uint16_t)island->island_id;
    int32_t spawned = 0;

    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    for (int32_t seed_z = island->voxel_min_z; seed_z <= island->voxel_max_z; seed_z++)
    {
        for (int32_t seed_y = island->voxel_min_y; seed_y <= island->voxel_max_y; seed_y++)
        {
            for (int32_t seed_x = island->voxel_min_x; seed_x <= island->voxel_max_x; seed_x++)
            {
                if (spawned >= max_bodies)
                    return spawned;

                size_t seed_gi = volume_voxel_linear_index(vol, seed_x, seed_y, seed_z);
                if (work->island_ids[seed_gi] != target_id || work->visited_gen[seed_gi] == consumed_gen)
                    continue;

                int32_t gmin_x = seed_x, gmax_x = seed_x;
                int32_t gmin_y = seed_y, gmax_y = seed_y;
                int32_t gmin_z = seed_z, gmax_z = seed_z;

                /* BFS with the work stack as a FIFO queue */
                work->visited_gen[seed_gi] = group_gen;
                work->stack[0] = volume_pack_voxel_pos(seed_x >> CHUNK_SIZE_BITS, seed_y >> CHUNK_SIZE_BITS,
                                                       seed_z >> CHUNK_SIZE_BITS, seed_x & CHUNK_SIZE_MASK,
                                                       seed_y & CHUNK_SIZE_MASK, seed_z & CHUNK_SIZE_MASK);
                int32_t front = 0, back = 1;

                while (front < back)
                {
                    int32_t cx, cy, cz, lx, ly, lz;
                    volume_unpack_voxel_pos(work->stack[front++], &cx, &cy, &cz, &lx, &ly, &lz);
                    int32_t vx = cx * CHUNK_SIZE + lx;
                    int32_t vy = cy * CHUNK_SIZE + ly;
                    int32_t vz = cz * CHUNK_SIZE + lz;

                    for (int32_t d = 0; d < 6; d++)
                    {
                        int32_t nx = vx + dx[d];
                        int32_t ny = vy + dy[d];
                        int32_t nz = vz + dz[d];
                        if (nx < island->voxel_min_x || nx > island->voxel_max_x ||
                            ny < island->voxel_min_y || ny > island->voxel_max_y ||
                            nz < island->voxel_min_z || nz > island->voxel_max_z)
                            continue;

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

                        size_t ngi = volume_voxel_linear_index(vol, nx, ny, nz);
                        if (work->island_ids[ngi] != target_id ||
                            work->visited_gen[ngi] == group_gen ||
                            work->visited_gen[ngi] == consumed_gen)
                            continue;

                        gmin_x = new_min_x; gmax_x = new_max_x;
                        gmin_y = new_min_y; gmax_y = new_max_y;
                        gmin_z = new_min_z; gmax_z = new_max_z;

                        work->visited_gen[ngi] = group_gen;
                        work->stack[back++] = volume_pack_voxel_pos(nx >> CHUNK_SIZE_BITS, ny >> CHUNK_SIZE_BITS,
                                                                    nz >> CHUNK_SIZE_BITS, nx & CHUNK_SIZE_MASK,
                                                                    ny & CHUNK_SIZE_MASK, nz & CHUNK_SIZE_MASK);
                    }
                }

                /* Copy the group out of its bounding box and mark it consumed */
                int32_t sub_ex = gmax_x - gmin_x + 1;
                int32_t sub_ey = gmax_y - gmin_y + 1;
                int32_t sub_ez = gmax_z - gmin_z + 1;

                uint8_t sub_buf[VOBJ_TOTAL_VOXELS];
                memset(sub_buf, 0, sizeof(sub_buf));

                for (int32_t gz = gmin_z; gz <= gmax_z; gz++)
                {
                    for (int32_t gy = gmin_y; gy <= gmax_y; gy++)
                    {
                        for (int32_t gx = gmin_x; gx <= gmax_x; gx++)
                        {
                            size_t gi = volume_voxel_linear_index(vol, gx, gy, gz);
                            if (work->visited_gen[gi] != group_gen)
                                continue;
                            work->visited_gen[gi] = consumed_gen;

                            int32_t ox = gx - gmin_x;
                            int32_t oy = gy - gmin_y;
                            int32_t oz = gz - gmin_z;
                            sub_buf[ox + oy * sub_ex + oz * sub_ex * sub_ey] = volume_voxel_material(vol, gi);
                        }
                    }
                }

                Vec3 sub_origin = vec3_create(
                    vol->bounds.min_x + (float)gmin_x * vol->voxel_size,
                    vol->bounds.min_y + (float)gmin_y * vol->voxel_size,
                    vol->bounds.min_z + (float)gmin_z * vol->voxel_size);

                int32_t obj_idx = voxel_object_world_add_from_voxels(
                    obj_world, sub_buf, sub_ex, sub_ey, sub_ez, sub_origin, vol->voxel_size);
                if (obj_idx >= 0)
                {
                    obj_world->objects[obj_idx].render_delay = 3;
                    if (result->bodies_spawned < DETACH_MAX_SPAWNED)
                        result->spawned_indices[result->bodies_spawned] = obj_idx;
                    result->bodies_spawned++;
                    spawned++;
                }
            }
        }
    }
    return spawned;
}

void detach_terrain_process(VoxelVolume *vol,
                            VoxelObjectWorld *obj_world,
                            const DetachConfig *config,
                            ConnectivityWorkBuffer *work,
                            DetachResult *result,
                            ParticleSystem *particles,
                            RngState *rng)
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

    /* Analyse every edit accumulated since the last pass, not just the last batch */
    float anchor_y = vol->bounds.min_y + config->anchor_y_offset;
    ConnectivityResult conn_result;
    volume_consume_pending_analysis(vol);
    connectivity_analyze_dirty(vol, anchor_y, work, &conn_result);

    /* If this pass cannot handle every floating island (result truncated, or the
     * per-tick limit), keep the analysed edits pending so the next pass finds the rest
     * instead of leaving them hovering. The body limit is not a reason: see below. */
    if (conn_result.truncated || conn_result.island_count > config->max_islands_per_tick)
    {
        volume_requeue_last_analysis(vol);
        local_result.analysis_requeued = true;
    }

    int32_t processed = 0;
    for (int32_t i = 0; i < conn_result.island_count && processed < config->max_islands_per_tick; i++)
    {
        IslandInfo *island = &conn_result.islands[i];
        local_result.islands_processed++;

        if (island->voxel_count < config->min_voxels_per_island)
        {
            connectivity_remove_island(vol, island, work);
            local_result.voxels_removed += island->voxel_count;
            processed++;
            continue;
        }

        if (island->voxel_count <= config->particle_voxel_threshold && particles && rng)
        {
            detach_spawn_island_particles(vol, island, work, particles, rng);
            connectivity_remove_island(vol, island, work);
            local_result.particles_spawned += island->voxel_count;
            local_result.voxels_removed += island->voxel_count;
            processed++;
            continue;
        }

        /* At the body limit the island crumbles to debris, as the part of an oversized
         * island beyond the limit does. Left in the terrain it would hover, and keeping
         * it pending re-ran the whole analysis every frame until bodies were freed. */
        if (active_bodies >= config->max_bodies_alive)
        {
            connectivity_remove_island(vol, island, work);
            local_result.voxels_removed += island->voxel_count;
            local_result.islands_skipped++;
            processed++;
            continue;
        }

        int32_t ext_size_x = island->voxel_max_x - island->voxel_min_x + 1;
        int32_t ext_size_y = island->voxel_max_y - island->voxel_min_y + 1;
        int32_t ext_size_z = island->voxel_max_z - island->voxel_min_z + 1;

        if (ext_size_x > VOBJ_GRID_SIZE || ext_size_y > VOBJ_GRID_SIZE || ext_size_z > VOBJ_GRID_SIZE)
        {
            int32_t spawned = detach_split_oversized_island(vol, obj_world, island, work,
                                                            config->max_bodies_alive - active_bodies,
                                                            &local_result);
            active_bodies += spawned;

            /* Whatever did not fit under the body limit goes as debris */
            connectivity_remove_island(vol, island, work);
            if (spawned == 0)
                local_result.voxels_removed += island->voxel_count;
            processed++;
            continue;
        }

        uint8_t extract_buf[VOBJ_TOTAL_VOXELS];
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
