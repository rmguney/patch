#include "melee.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static Vec3 prop_palette[] = {
    { 0.95f, 0.55f, 0.65f },
    { 0.55f, 0.85f, 0.85f },
    { 0.98f, 0.85f, 0.75f },
    { 0.70f, 0.90f, 0.80f },
    { 0.95f, 0.75f, 0.80f },
    { 0.75f, 0.80f, 0.95f },
    { 0.60f, 0.80f, 0.80f },
    { 0.90f, 0.70f, 0.75f },
    { 0.85f, 0.90f, 0.95f },
    { 0.95f, 0.80f, 0.85f },
};
static const int32_t PROP_PALETTE_COUNT = sizeof(prop_palette) / sizeof(prop_palette[0]);

static float random_float(float min_val, float max_val) {
    return min_val + (float)rand() / (float)RAND_MAX * (max_val - min_val);
}

static int32_t get_chunk_id(float x, float z) {
    int32_t chunk_size = 8;
    int32_t cx = (int32_t)(x / chunk_size);
    int32_t cz = (int32_t)(z / chunk_size);
    return cx * 1000 + cz;
}

static bool chunk_spawned(MeleeData* data, int32_t chunk_id) {
    for (int32_t i = 0; i < data->spawned_chunk_count; i++) {
        if (data->spawned_chunks[i] == chunk_id) return true;
    }
    return false;
}

static void mark_chunk_spawned(MeleeData* data, int32_t chunk_id) {
    if (data->spawned_chunk_count >= 1024) {
        data->spawned_chunk_count = 0;
    }
    data->spawned_chunks[data->spawned_chunk_count++] = chunk_id;
}

static void spawn_chunk_props(MeleeData* data, int32_t chunk_x, int32_t chunk_z, float floor_y) {
    float chunk_size = 11.0f;
    float base_x = chunk_x * chunk_size;
    float base_z = chunk_z * chunk_size;

    int32_t prop_count = 2 + rand() % 3;

    for (int32_t i = 0; i < prop_count; i++) {
        float x = base_x + random_float(1.0f, chunk_size - 1.0f);
        float z = base_z + random_float(1.0f, chunk_size - 1.0f);

        Vec3 color = prop_palette[rand() % PROP_PALETTE_COUNT];
        int32_t shape_type = rand() % 7;

        if (shape_type == 0) {
            float radius = random_float(0.3f, 0.5f);
            voxel_object_world_add_sphere(data->vobj_world, vec3_create(x, floor_y + radius, z), radius, color);
        } else if (shape_type == 1) {
            float width = random_float(0.3f, 0.5f);
            float height = random_float(0.8f, 1.8f);
            float depth = random_float(0.3f, 0.5f);
            voxel_object_world_add_box(data->vobj_world,
                vec3_create(x, floor_y + height * 0.5f, z),
                vec3_create(width, height, depth), color);
        } else if (shape_type == 2) {
            float size = random_float(0.4f, 0.7f);
            voxel_object_world_add_box(data->vobj_world,
                vec3_create(x, floor_y + size * 0.5f, z),
                vec3_create(size, size, size), color);
        } else if (shape_type == 3) {
            float radius = random_float(0.25f, 0.4f);
            float height = random_float(1.2f, 2.0f);
            voxel_object_world_add_cylinder(data->vobj_world,
                vec3_create(x, floor_y, z), radius, height, color);
        } else if (shape_type == 4) {
            float major = random_float(0.45f, 0.65f);
            float tube = random_float(0.14f, 0.22f);
            voxel_object_world_add_torus(data->vobj_world,
                vec3_create(x, floor_y + tube + 0.04f, z),
                major, tube, color);
        } else if (shape_type == 5) {
            float outer = random_float(0.55f, 0.85f);
            float inner = outer * random_float(0.45f, 0.65f);
            float thick = outer * random_float(0.08f, 0.13f);
            voxel_object_world_add_tesseract(data->vobj_world,
                vec3_create(x, floor_y + outer, z),
                outer, inner, thick, color);
        } else if (shape_type == 6) {
            float radius = random_float(0.30f, 0.48f);
            float height = random_float(1.0f, 2.2f);
            voxel_object_world_add_crystal(data->vobj_world,
                vec3_create(x, floor_y, z), radius, height, color);
        }
    }
}

static void cleanup_far_props(MeleeData* data, Vec3 player_pos) {
    float max_dist = 30.0f;
    float max_dist_sq = max_dist * max_dist;

    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++) {
        if (!data->vobj_world->objects[i].active) continue;

        Vec3 diff = vec3_sub(data->vobj_world->objects[i].position, player_pos);
        diff.y = 0.0f;
        float dist_sq = diff.x * diff.x + diff.z * diff.z;

        if (dist_sq > max_dist_sq) {
            voxel_object_world_remove(data->vobj_world, i);
        }
    }
}

static void spawn_props_near_player(MeleeData* data, Vec3 player_pos, float floor_y) {
    cleanup_far_props(data, player_pos);

    int32_t chunk_size = 11;
    int32_t player_cx = (int32_t)floorf(player_pos.x / chunk_size);
    int32_t player_cz = (int32_t)floorf(player_pos.z / chunk_size);

    for (int32_t dx = -1; dx <= 1; dx++) {
        for (int32_t dz = -1; dz <= 1; dz++) {
            int32_t cx = player_cx + dx;
            int32_t cz = player_cz + dz;
            int32_t chunk_id = cx * 10000 + cz;

            if (!chunk_spawned(data, chunk_id)) {
                spawn_chunk_props(data, cx, cz, floor_y);
                mark_chunk_spawned(data, chunk_id);
            }
        }
    }
}

static void spawn_enemy(MeleeData* data, const Bounds3D* bounds) {
    if (data->enemy_count >= MELEE_MAX_ENEMIES) return;

    float spawn_distance = 12.0f;
    float angle = random_float(0.0f, 2.0f * 3.14159265f);

    Vec3 spawn_pos = vec3_create(
        data->player.position.x + spawn_distance * cosf(angle),
        bounds->min_y,
        data->player.position.z + spawn_distance * sinf(angle)
    );

    Enemy* enemy = &data->enemies[data->enemy_count];
    enemy_init(enemy, spawn_pos, data->next_enemy_id++);
    data->enemy_count++;
}

static void spawn_hit_particles(MeleeData* data, Vec3 pos, Vec3 color, Vec3 dir, int32_t count) {
    for (int32_t i = 0; i < count; i++) {
        Particle* p = particle_system_add_slot(data->particles);
        if (!p) break;

        Vec3 offset = vec3_create(
            random_float(-0.1f, 0.1f),
            random_float(-0.1f, 0.1f),
            random_float(-0.1f, 0.1f)
        );

        p->position = vec3_add(pos, offset);

        Vec3 vel = vec3_add(dir, vec3_create(
            random_float(-1.0f, 1.0f),
            random_float(0.5f, 2.0f),
            random_float(-1.0f, 1.0f)
        ));
        p->velocity = vec3_scale(vel, random_float(2.0f, 5.0f));

        p->color = color;
        p->radius = random_float(0.03f, 0.08f);
        p->lifetime = 0.0f;
        p->active = true;
        p->settled = false;
    }
}

static void spawn_death_particles(MeleeData* data, Enemy* enemy) {
    Vec3 color = vec3_create(0.85f, 0.45f, 0.45f);

    for (int32_t i = 0; i < enemy->model.voxel_count; i++) {
        if (!enemy->model.voxels[i].active) continue;

        Particle* p = particle_system_add_slot(data->particles);
        if (!p) break;

        float sin_yaw = sinf(enemy->yaw);
        float cos_yaw = cosf(enemy->yaw);
        Vec3 local = enemy->model.voxels[i].local_offset;
        Vec3 world_offset;
        world_offset.x = local.x * cos_yaw - local.z * sin_yaw;
        world_offset.y = local.y;
        world_offset.z = local.x * sin_yaw + local.z * cos_yaw;

        p->position = vec3_add(enemy->position, world_offset);
        p->velocity = vec3_create(
            random_float(-2.0f, 2.0f),
            random_float(2.0f, 5.0f),
            random_float(-2.0f, 2.0f)
        );
        p->color = color;
        p->radius = 0.06f;
        p->lifetime = 0.0f;
        p->active = true;
        p->settled = false;
    }
}

static void remove_enemy(MeleeData* data, int32_t index) {
    if (index < data->enemy_count - 1) {
        data->enemies[index] = data->enemies[data->enemy_count - 1];
    }
    data->enemy_count--;
}

static void melee_init(Scene* scene) {
    MeleeData* data = (MeleeData*)scene->user_data;

    Vec3 start_pos = vec3_create(0.0f, scene->bounds.min_y, 0.0f);
    player_init(&data->player, start_pos);

    data->max_dead_bodies = MELEE_DEFAULT_MAX_DEAD_BODIES;
    data->dead_body_count = 0;
    data->spawned_chunk_count = 0;

    for (int32_t i = 0; i < 4; i++) {
        spawn_enemy(data, &scene->bounds);
    }

    spawn_props_near_player(data, data->player.position, scene->bounds.min_y);
}

static void melee_destroy_impl(Scene* scene) {
    MeleeData* data = (MeleeData*)scene->user_data;

    if (data->particles) {
        particle_system_destroy(data->particles);
    }
    if (data->vobj_world) {
        voxel_object_world_destroy(data->vobj_world);
    }

    free(data);
    free(scene);
}

static void melee_update(Scene* scene, float dt) {
    MeleeData* data = (MeleeData*)scene->user_data;

    player_update(&data->player, &data->input, dt);
    if (!data->player.is_dead) {
        data->survival_time += dt;
    }

    if (!data->player.is_dead) {
        Vec3 pickup_color;
        float pickup_radius = 0.3f;
        if (particle_system_pickup_nearest(data->particles, data->player.position, pickup_radius, &pickup_color)) {
            humanoid_heal_voxel(&data->player.model, pickup_color);
        }
    }

    float floor_y = scene->bounds.min_y;

    for (int32_t i = 0; i < data->enemy_count; i++) {
        Enemy* enemy = &data->enemies[i];
        if (!enemy->active) continue;
        enemy->steering = vec3_zero();

        if (enemy->state == ENEMY_STATE_DYING || enemy->state == ENEMY_STATE_DEAD) continue;
        if (enemy->state == ENEMY_STATE_HELD) continue;

        for (int32_t j = 0; j < data->enemy_count; j++) {
            if (i == j) continue;
            Enemy* other = &data->enemies[j];
            if (!other->active) continue;
            if (other->state == ENEMY_STATE_DYING || other->state == ENEMY_STATE_DEAD) continue;

            Vec3 diff = vec3_sub(enemy->position, other->position);
            diff.y = 0.0f;
            float d = vec3_length(diff);
            float sep_radius = 1.5f;

            if (d < sep_radius && d > 0.01f) {
                float strength = (sep_radius - d) / sep_radius;
                strength *= strength;
                Vec3 push = vec3_scale(diff, strength / d);
                enemy->steering = vec3_add(enemy->steering, push);
            }
        }
    }

    for (int32_t i = 0; i < data->enemy_count; i++) {
        Enemy* enemy = &data->enemies[i];
        if (!enemy->active) continue;

        if (enemy->state == ENEMY_STATE_DYING || enemy->state == ENEMY_STATE_DEAD) {
            enemy_update_death(enemy, floor_y, dt);
            continue;
        }

        if (enemy->state == ENEMY_STATE_HELD) {
            continue;
        }

        enemy_update(enemy, data->player.position, dt);
    }

    for (int32_t i = 0; i < data->enemy_count; i++) {
        Enemy* enemy = &data->enemies[i];
        if (!enemy->active) continue;

        Vec3 to_enemy = vec3_sub(enemy->position, data->player.position);
        to_enemy.y = 0.0f;
        float dist = vec3_length(to_enemy);
        float player_radius = player_get_collision_radius(&data->player);
        float min_dist = player_radius + enemy_get_collision_radius(enemy);

        if (dist < min_dist && dist > 0.001f) {
            Vec3 push_dir = vec3_scale(to_enemy, 1.0f / dist);
            float overlap = min_dist - dist;
            data->player.position = vec3_sub(data->player.position, vec3_scale(push_dir, overlap * 0.3f));
            enemy->position = vec3_add(enemy->position, vec3_scale(push_dir, overlap * 0.7f));
        }

        for (int32_t j = i + 1; j < data->enemy_count; j++) {
            Enemy* other = &data->enemies[j];
            if (!other->active) continue;

            Vec3 to_other = vec3_sub(other->position, enemy->position);
            to_other.y = 0.0f;
            float d = vec3_length(to_other);
            float min_d = enemy_get_collision_radius(enemy) + enemy_get_collision_radius(other);

            if (d < min_d && d > 0.001f) {
                Vec3 push = vec3_scale(to_other, (min_d - d) * 0.5f / d);
                enemy->position = vec3_sub(enemy->position, push);
                other->position = vec3_add(other->position, push);
            }
        }
    }

    float player_radius = player_get_collision_radius(&data->player);
    for (int32_t i = 0; i < data->vobj_world->object_count; i++) {
        VoxelObject* obj = &data->vobj_world->objects[i];
        if (!obj->active) continue;

        Vec3 to_obj = vec3_sub(obj->position, data->player.position);
        to_obj.y = 0.0f;
        float dist = vec3_length(to_obj);
        float min_dist = player_radius + obj->radius * 0.7f;

        if (dist < min_dist && dist > 0.001f) {
            Vec3 push_dir = vec3_scale(to_obj, 1.0f / dist);
            float overlap = min_dist - dist;
            data->player.position = vec3_sub(data->player.position, vec3_scale(push_dir, overlap * 0.3f));
            obj->position = vec3_add(obj->position, vec3_scale(push_dir, overlap * 0.7f));
            obj->velocity = vec3_add(obj->velocity, vec3_scale(push_dir, 2.0f));
        }

        for (int32_t j = 0; j < data->enemy_count; j++) {
            Enemy* enemy = &data->enemies[j];
            if (!enemy->active || enemy->state == ENEMY_STATE_DEAD) continue;

            Vec3 to_enemy = vec3_sub(obj->position, enemy->position);
            to_enemy.y = 0.0f;
            float d = vec3_length(to_enemy);
            float min_d = enemy_get_collision_radius(enemy) + obj->radius * 0.7f;

            if (d < min_d && d > 0.001f) {
                Vec3 push = vec3_scale(to_enemy, 1.0f / d);
                float ov = min_d - d;
                enemy->position = vec3_sub(enemy->position, vec3_scale(push, ov * 0.3f));
                obj->position = vec3_add(obj->position, vec3_scale(push, ov * 0.7f));
                obj->velocity = vec3_add(obj->velocity, vec3_scale(push, 1.5f));
            }
        }
    }

    if (data->player.is_punching && data->player.punch_timer > PLAYER_PUNCH_COOLDOWN * 0.5f) {
        CapsuleHitbox punch_hitbox = player_get_punch_hitbox(&data->player);

        for (int32_t i = 0; i < data->enemy_count; i++) {
            Enemy* enemy = &data->enemies[i];
            if (!enemy->active || enemy->state == ENEMY_STATE_DEAD) continue;
            if (enemy->hit_this_punch) continue;

            Vec3 enemy_center = enemy_get_body_center(enemy);
            float enemy_radius = enemy_get_collision_radius(enemy) + 0.3f;

            if (combat_capsule_vs_sphere(&punch_hitbox, enemy_center, enemy_radius)) {
                Vec3 hit_point = combat_closest_point_on_segment(enemy_center, punch_hitbox.start, punch_hitbox.end);

                enemy->hit_this_punch = true;

                Vec3 hit_dir = vec3_normalize(vec3_sub(enemy->position, data->player.position));

                int32_t destroyed = enemy_damage_at_point(enemy, hit_point, PLAYER_PUNCH_DAMAGE, hit_dir,
                    data->destroyed_positions, data->destroyed_colors, 64);

                for (int32_t d = 0; d < destroyed; d++) {
                    spawn_hit_particles(data, data->destroyed_positions[d], data->destroyed_colors[d], hit_dir, 1);
                }

                HumanoidPose pose = enemy_get_pose(enemy);
                Vec3 enemy_color = vec3_create(0.85f, 0.45f, 0.45f);
                int32_t dropped = humanoid_check_connectivity(&enemy->model, enemy->position, &pose, enemy_color,
                    &data->destroyed_positions[destroyed], &data->destroyed_colors[destroyed], 64 - destroyed);

                data->destroyed_cubes += destroyed + dropped;

                for (int32_t d = 0; d < dropped; d++) {
                    spawn_hit_particles(data, data->destroyed_positions[destroyed + d],
                                       data->destroyed_colors[destroyed + d], vec3_create(0, -1, 0), 1);
                }

                if (destroyed > 0 || dropped > 0) {
                    enemy->position = vec3_add(enemy->position, vec3_scale(hit_dir, 0.3f));
                }

                if (humanoid_should_die(&enemy->model) &&
                    enemy->state != ENEMY_STATE_DYING && enemy->state != ENEMY_STATE_DEAD) {
                    enemy_start_dying(enemy);
                    data->score += 100;
                    data->kills++;
                    data->dead_body_count++;
                }
            }
        }

        for (int32_t i = 0; i < data->vobj_world->object_count; i++) {
            VoxelObject* obj = &data->vobj_world->objects[i];
            if (!obj->active || data->prop_hit_this_punch[i]) continue;

            Vec3 obj_center = vec3_add(obj->position, obj->shape_center_offset);
            float obj_radius = fmaxf(obj->shape_half_extents.x, fmaxf(obj->shape_half_extents.y, obj->shape_half_extents.z));

            if (combat_capsule_vs_sphere(&punch_hitbox, obj_center, obj_radius)) {
                Vec3 hit_point = combat_closest_point_on_segment(obj_center, punch_hitbox.start, punch_hitbox.end);
                data->prop_hit_this_punch[i] = true;

                Vec3 hit_dir = vec3_normalize(vec3_sub(obj->position, data->player.position));

                int32_t destroyed = voxel_object_destroy_at_point(data->vobj_world, i, hit_point, 0.6f, 5,
                    data->destroyed_positions, data->destroyed_colors, 64);

                data->destroyed_cubes += destroyed;

                for (int32_t d = 0; d < destroyed; d++) {
                    spawn_hit_particles(data, data->destroyed_positions[d], data->destroyed_colors[d], hit_dir, 1);
                }

                obj->velocity = vec3_add(obj->velocity, vec3_scale(hit_dir, 5.0f));
                obj->angular_velocity = vec3_add(obj->angular_velocity, vec3_create(
                    ((float)rand() / RAND_MAX - 0.5f) * 3.0f,
                    ((float)rand() / RAND_MAX - 0.5f) * 3.0f,
                    ((float)rand() / RAND_MAX - 0.5f) * 3.0f
                ));
            }
        }
    } else {
        for (int32_t i = 0; i < data->enemy_count; i++) {
            enemy_reset_punch_state(&data->enemies[i]);
        }
        for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++) {
            data->prop_hit_this_punch[i] = false;
        }
    }

    for (int32_t i = 0; i < data->enemy_count; i++) {
        Enemy* enemy = &data->enemies[i];
        if (!enemy->active || enemy->state != ENEMY_STATE_ATTACK) continue;
        if (enemy->hit_this_attack) continue;
        if (enemy->state_timer > (ENEMY_ATTACK_DURATION - ENEMY_ATTACK_WINDUP)) continue;

        CapsuleHitbox enemy_punch = enemy_get_punch_hitbox(enemy);
        Vec3 player_center = player_get_body_center(&data->player);
        float player_hit_radius = player_get_collision_radius(&data->player) + 0.2f;

        if (combat_capsule_vs_sphere(&enemy_punch, player_center, player_hit_radius)) {
            Vec3 hit_point = combat_closest_point_on_segment(player_center, enemy_punch.start, enemy_punch.end);
            Vec3 hit_dir = vec3_normalize(vec3_sub(data->player.position, enemy->position));

            int32_t destroyed = player_damage_at_point(&data->player, hit_point, 15.0f, hit_dir,
                data->destroyed_positions, data->destroyed_colors, 64);

            for (int32_t d = 0; d < destroyed; d++) {
                spawn_hit_particles(data, data->destroyed_positions[d], data->destroyed_colors[d], hit_dir, 1);
            }

            HumanoidPose pose = player_get_pose(&data->player);
            Vec3 player_color = vec3_create(0.20f, 0.60f, 0.85f);
            int32_t dropped = humanoid_check_connectivity(&data->player.model, data->player.position, &pose, player_color,
                &data->destroyed_positions[destroyed], &data->destroyed_colors[destroyed], 64 - destroyed);

            for (int32_t d = 0; d < dropped; d++) {
                spawn_hit_particles(data, data->destroyed_positions[destroyed + d],
                                   data->destroyed_colors[destroyed + d], vec3_create(0, -1, 0), 1);
            }

            if (destroyed > 0 || dropped > 0) {
                data->player.position = vec3_add(data->player.position, vec3_scale(hit_dir, 0.2f));
            }

            if (humanoid_should_die(&data->player.model)) {
                data->player.is_dead = true;
            }

            enemy->hit_this_attack = true;
            enemy->state = ENEMY_STATE_CHASE;
        }
    }

    if (data->dead_body_count > data->max_dead_bodies) {
        float oldest_time = -1.0f;
        int32_t oldest_idx = -1;
        for (int32_t i = 0; i < data->enemy_count; i++) {
            if (data->enemies[i].state == ENEMY_STATE_DEAD && data->enemies[i].death_time > oldest_time) {
                oldest_time = data->enemies[i].death_time;
                oldest_idx = i;
            }
        }
        if (oldest_idx >= 0) {
            remove_enemy(data, oldest_idx);
            data->dead_body_count--;
        }
    }

    static bool was_grabbing = false;
    bool grab_pressed = data->input.grab && !was_grabbing;
    was_grabbing = data->input.grab;

    if (grab_pressed) {
        if (data->player.is_holding && data->player.held_enemy_id >= 0) {
            for (int32_t i = 0; i < data->enemy_count; i++) {
                if (data->enemies[i].id == data->player.held_enemy_id) {
                    float sin_yaw = sinf(data->player.yaw);
                    float cos_yaw = cosf(data->player.yaw);
                    Vec3 throw_dir = vec3_create(sin_yaw, 0.3f, cos_yaw);
                    data->enemies[i].velocity = vec3_scale(throw_dir, 12.0f);
                    data->enemies[i].state = ENEMY_STATE_STAGGER;
                    data->enemies[i].state_timer = 0.8f;
                    break;
                }
            }
            data->player.is_holding = false;
            data->player.held_enemy_id = -1;
        } else {
            CapsuleHitbox grab_hitbox = player_get_grab_hitbox(&data->player);

            for (int32_t i = 0; i < data->enemy_count; i++) {
                Enemy* enemy = &data->enemies[i];
                if (!enemy->active || enemy->state == ENEMY_STATE_DEAD || enemy->state == ENEMY_STATE_DYING) continue;

                Vec3 enemy_center = enemy_get_body_center(enemy);
                float enemy_radius = enemy_get_collision_radius(enemy) + 0.3f;

                if (combat_capsule_vs_sphere(&grab_hitbox, enemy_center, enemy_radius)) {
                    data->player.is_holding = true;
                    data->player.held_enemy_id = enemy->id;
                    enemy->state = ENEMY_STATE_HELD;
                    break;
                }
            }
        }
    }

    if (data->player.is_holding && data->player.held_enemy_id >= 0) {
        Vec3 hold_pos = player_get_right_hand(&data->player);
        hold_pos.y = floor_y + 0.5f;

        for (int32_t i = 0; i < data->enemy_count; i++) {
            if (data->enemies[i].id == data->player.held_enemy_id) {
                bool died = enemy_update_held(&data->enemies[i], hold_pos, data->player.velocity, dt);

                if (died) {
                    data->player.is_holding = false;
                    data->player.held_enemy_id = -1;
                    break;
                }

                if (data->enemies[i].position.y < floor_y) {
                    data->enemies[i].position.y = floor_y;
                    data->enemies[i].velocity.y = -data->enemies[i].velocity.y * 0.3f;
                }
                break;
            }
        }
    }

    particle_system_update(data->particles, dt);

    voxel_object_world_update(data->vobj_world, dt);

    spawn_props_near_player(data, data->player.position, floor_y);

    data->spawn_timer += dt;
    float spawn_rate = data->spawn_interval / (1.0f + data->difficulty * 0.1f);
    if (data->spawn_timer >= spawn_rate && data->enemy_count < MELEE_MAX_ENEMIES) {
        spawn_enemy(data, &scene->bounds);
        data->spawn_timer = 0.0f;
        data->difficulty += 0.1f;
    }
}

static void melee_handle_input(Scene* scene, float mouse_x, float mouse_y, bool left_down, bool right_down) {
    (void)mouse_x;
    (void)mouse_y;

    MeleeData* data = (MeleeData*)scene->user_data;
    data->input.punch = left_down;
    data->input.grab = right_down;
}

static const char* melee_get_name(Scene* scene) {
    (void)scene;
    return "Melee";
}

static const SceneVTable melee_vtable = {
    .init = melee_init,
    .destroy = melee_destroy_impl,
    .update = melee_update,
    .handle_input = melee_handle_input,
    .render = NULL,
    .get_name = melee_get_name
};

Scene* melee_scene_create(Bounds3D bounds) {
    Scene* scene = (Scene*)calloc(1, sizeof(Scene));
    if (!scene) return NULL;

    MeleeData* data = (MeleeData*)calloc(1, sizeof(MeleeData));
    if (!data) {
        free(scene);
        return NULL;
    }

    Bounds3D infinite_bounds = bounds;
    infinite_bounds.min_x = -1000.0f;
    infinite_bounds.max_x = 1000.0f;
    infinite_bounds.min_z = -1000.0f;
    infinite_bounds.max_z = 1000.0f;

    data->particles = particle_system_create(infinite_bounds);
    if (!data->particles) {
        free(data);
        free(scene);
        return NULL;
    }

    data->vobj_world = voxel_object_world_create(infinite_bounds);
    if (!data->vobj_world) {
        particle_system_destroy(data->particles);
        free(data);
        free(scene);
        return NULL;
    }

    data->enemy_count = 0;
    data->next_enemy_id = 1;
    data->survival_time = 0.0f;
    data->score = 0;
    data->kills = 0;
    data->destroyed_cubes = 0;
    data->spawn_timer = 0.0f;
    data->spawn_interval = 1.5f;
    data->difficulty = 1.0f;

    memset(&data->input, 0, sizeof(PlayerInput));

    scene->vtable = &melee_vtable;
    scene->bounds = bounds;
    scene->user_data = data;

    return scene;
}

void melee_set_input(Scene* scene, bool w, bool a, bool s, bool d, bool left_click, bool right_click) {
    if (!scene || !scene->user_data) return;
    MeleeData* data = (MeleeData*)scene->user_data;
    data->input.move_forward = w;
    data->input.move_left = a;
    data->input.move_backward = s;
    data->input.move_right = d;
    data->input.punch = left_click;
    data->input.grab = right_click;
}

MeleeData* melee_get_data(Scene* scene) {
    if (!scene) return NULL;
    return (MeleeData*)scene->user_data;
}