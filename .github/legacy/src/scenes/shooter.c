#include "shooter.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SHOOTER_BULLET_SPEED 45.0f
#define SHOOTER_BULLET_LIFETIME 2.0f
#define SHOOTER_BULLET_RADIUS 0.12f
#define SHOOTER_BULLET_DAMAGE 18.0f
#define SHOOTER_FIRE_COOLDOWN 0.12f

static bool sweep_sphere_vs_sphere(Vec3 start, Vec3 end, float radius, Vec3 center, float center_radius, float* out_t, Vec3* out_hit) {
    Vec3 seg = vec3_sub(end, start);
    float seg_len_sq = vec3_dot(seg, seg);
    if (seg_len_sq < 1e-8f) {
        float r = radius + center_radius;
        Vec3 d = vec3_sub(start, center);
        float dist_sq = vec3_dot(d, d);
        if (dist_sq <= r * r) {
            if (out_t) *out_t = 0.0f;
            if (out_hit) *out_hit = start;
            return true;
        }
        return false;
    }

    Vec3 closest = combat_closest_point_on_segment(center, start, end);
    Vec3 diff = vec3_sub(closest, center);
    float r = radius + center_radius;
    float dist_sq = vec3_dot(diff, diff);
    if (dist_sq > r * r) return false;

    float t = 0.0f;
    Vec3 to_closest = vec3_sub(closest, start);
    float proj = vec3_dot(to_closest, seg) / seg_len_sq;
    t = clampf(proj, 0.0f, 1.0f);

    if (out_t) *out_t = t;
    if (out_hit) *out_hit = vec3_add(start, vec3_scale(seg, t));
    return true;
}

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

static bool chunk_spawned(ShooterData* data, int32_t chunk_id) {
    for (int32_t i = 0; i < data->spawned_chunk_count; i++) {
        if (data->spawned_chunks[i] == chunk_id) return true;
    }
    return false;
}

static void mark_chunk_spawned(ShooterData* data, int32_t chunk_id) {
    if (data->spawned_chunk_count >= 1024) {
        data->spawned_chunk_count = 0;
    }
    data->spawned_chunks[data->spawned_chunk_count++] = chunk_id;
}

static void spawn_chunk_props(ShooterData* data, int32_t chunk_x, int32_t chunk_z, float floor_y) {
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

static void cleanup_far_props(ShooterData* data, Vec3 player_pos) {
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

static void spawn_props_near_player(ShooterData* data, Vec3 player_pos, float floor_y) {
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

static void spawn_enemy(ShooterData* data, const Bounds3D* bounds) {
    if (data->enemy_count >= SHOOTER_MAX_ENEMIES) return;

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

static void spawn_hit_particles(ShooterData* data, Vec3 pos, Vec3 color, Vec3 dir, int32_t count) {
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

static void spawn_projectile(ShooterData* data) {
    for (int32_t i = 0; i < SHOOTER_MAX_PROJECTILES; i++) {
        Projectile* p = &data->projectiles[i];
        if (p->active) continue;

        Vec3 shoulder = player_get_right_shoulder(&data->player);

        float yaw = data->player.yaw;
        float yaw_spread = random_float(-0.03f, 0.03f);
        float pitch_spread = random_float(-0.01f, 0.01f);
        float cp = cosf(pitch_spread);
        Vec3 aim_dir = vec3_normalize(vec3_create(-sinf(yaw + yaw_spread) * cp, sinf(pitch_spread), cosf(yaw + yaw_spread) * cp));

        p->position = vec3_add(shoulder, vec3_scale(aim_dir, 0.45f));
        p->velocity = vec3_scale(aim_dir, SHOOTER_BULLET_SPEED);
        p->lifetime = SHOOTER_BULLET_LIFETIME;
        p->radius = SHOOTER_BULLET_RADIUS;
        p->active = true;
        break;
    }
}

static void remove_enemy(ShooterData* data, int32_t index) {
    if (index < data->enemy_count - 1) {
        data->enemies[index] = data->enemies[data->enemy_count - 1];
    }
    data->enemy_count--;
}

static void shooter_init(Scene* scene) {
    ShooterData* data = (ShooterData*)scene->user_data;

    Vec3 start_pos = vec3_create(0.0f, scene->bounds.min_y, 0.0f);
    player_init(&data->player, start_pos);

    data->aim_origin = vec3_zero();
    data->aim_dir = vec3_create(0.0f, 0.0f, 1.0f);
    data->aim_valid = false;

    data->aiming = false;

    data->max_dead_bodies = 100;
    data->dead_body_count = 0;
    data->spawned_chunk_count = 0;

    for (int32_t i = 0; i < 4; i++) {
        spawn_enemy(data, &scene->bounds);
    }

    spawn_props_near_player(data, data->player.position, scene->bounds.min_y);
}

static bool shooter_compute_aim_yaw(const ShooterData* data, float plane_y, float* out_yaw) {
    if (!data->aim_valid) return false;

    float denom = data->aim_dir.y;
    if (fabsf(denom) < 1e-5f) return false;

    float t = (plane_y - data->aim_origin.y) / denom;
    if (t <= 0.0f) return false;

    Vec3 hit = vec3_add(data->aim_origin, vec3_scale(data->aim_dir, t));
    Vec3 to = vec3_sub(hit, data->player.position);
    to.y = 0.0f;

    float len = vec3_length(to);
    if (len < 1e-4f) return false;

    Vec3 dir = vec3_scale(to, 1.0f / len);
    *out_yaw = atan2f(-dir.x, dir.z);
    return true;
}

static void shooter_destroy_impl(Scene* scene) {
    ShooterData* data = (ShooterData*)scene->user_data;

    if (data->particles) {
        particle_system_destroy(data->particles);
    }
    if (data->vobj_world) {
        voxel_object_world_destroy(data->vobj_world);
    }

    free(data);
    free(scene);
}

static void shooter_update(Scene* scene, float dt) {
    ShooterData* data = (ShooterData*)scene->user_data;
    float floor_y = scene->bounds.min_y;

    player_update(&data->player, &data->input, dt);
    if (!data->player.is_dead) {
        data->survival_time += dt;
    }

    if (!data->player.is_dead && data->aiming) {
        float aim_yaw;
        if (shooter_compute_aim_yaw(data, data->player.position.y, &aim_yaw)) {
            data->player.yaw = aim_yaw;
        }
    }

    if (!data->player.is_dead) {
        Vec3 pickup_color;
        float pickup_radius = 0.3f;
        if (particle_system_pickup_nearest(data->particles, data->player.position, pickup_radius, &pickup_color)) {
            humanoid_heal_voxel(&data->player.model, pickup_color);
        }
    }

    data->shoot_cooldown -= dt;
    if (!data->player.is_dead && data->input.punch && data->shoot_cooldown <= 0.0f) {
        spawn_projectile(data);
        data->shoot_cooldown = SHOOTER_FIRE_COOLDOWN;
    }

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

    for (int32_t i = 0; i < SHOOTER_MAX_PROJECTILES; i++) {
        Projectile* proj = &data->projectiles[i];
        if (!proj->active) continue;

        proj->lifetime -= dt;
        if (proj->lifetime <= 0.0f) {
            proj->active = false;
            continue;
        }

        Vec3 start = proj->position;
        Vec3 end = vec3_add(proj->position, vec3_scale(proj->velocity, dt));

        bool hit = false;
        Vec3 hit_point = end;
        float hit_t = 1.0f;
        int32_t hit_enemy = -1;
        int32_t hit_object = -1;

        float floor_plane = floor_y + proj->radius;
        if (start.y > floor_plane && end.y <= floor_plane) {
            float denom = start.y - end.y;
            if (fabsf(denom) > 1e-6f) {
                float t = (start.y - floor_plane) / denom;
                t = clampf(t, 0.0f, 1.0f);
                hit = true;
                hit_t = t;
                hit_point = vec3_add(start, vec3_scale(vec3_sub(end, start), t));
                hit_enemy = -1;
                hit_object = -1;
            }
        }

        for (int32_t e = 0; e < data->enemy_count; e++) {
            Enemy* enemy = &data->enemies[e];
            if (!enemy->active || enemy->state == ENEMY_STATE_DEAD) continue;

            Vec3 enemy_center = enemy_get_body_center(enemy);
            float enemy_radius = enemy_get_collision_radius(enemy) + 0.25f;

            float t;
            Vec3 pt;
            if (sweep_sphere_vs_sphere(start, end, proj->radius, enemy_center, enemy_radius, &t, &pt)) {
                if (!hit || t < hit_t) {
                    hit = true;
                    hit_t = t;
                    hit_point = pt;
                    hit_enemy = e;
                    hit_object = -1;
                }
            }
        }

        for (int32_t o = 0; o < VOBJ_MAX_OBJECTS; o++) {
            VoxelObject* obj = &data->vobj_world->objects[o];
            if (!obj->active) continue;

            Vec3 obj_center = vec3_add(obj->position, obj->shape_center_offset);
            float obj_radius = obj->radius;
            if (obj_radius <= 0.0f) {
                obj_radius = fmaxf(obj->shape_half_extents.x, fmaxf(obj->shape_half_extents.y, obj->shape_half_extents.z));
            }

            float t;
            Vec3 pt;
            if (sweep_sphere_vs_sphere(start, end, proj->radius, obj_center, obj_radius, &t, &pt)) {
                if (!hit || t < hit_t) {
                    hit = true;
                    hit_t = t;
                    hit_point = pt;
                    hit_object = o;
                    hit_enemy = -1;
                }
            }
        }

        proj->position = end;

        if (hit_enemy >= 0) {
            Enemy* enemy = &data->enemies[hit_enemy];
                Vec3 hit_dir = vec3_normalize(vec3_sub(enemy->position, data->player.position));
                int32_t destroyed = enemy_damage_at_point(enemy, hit_point, SHOOTER_BULLET_DAMAGE, hit_dir,
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

                if (humanoid_should_die(&enemy->model) &&
                    enemy->state != ENEMY_STATE_DYING && enemy->state != ENEMY_STATE_DEAD) {
                    enemy_start_dying(enemy);
                    data->dead_body_count++;
                }

        } else if (hit_object >= 0) {
            VoxelObject* obj = &data->vobj_world->objects[hit_object];
            Vec3 hit_dir = vec3_normalize(vec3_sub(obj->position, data->player.position));
            int32_t destroyed = voxel_object_destroy_at_point(data->vobj_world, hit_object, hit_point, 0.55f, 6,
                data->destroyed_positions, data->destroyed_colors, 64);

            data->destroyed_cubes += destroyed;

            for (int32_t d = 0; d < destroyed; d++) {
                spawn_hit_particles(data, data->destroyed_positions[d], data->destroyed_colors[d], hit_dir, 1);
            }

            obj->velocity = vec3_add(obj->velocity, vec3_scale(hit_dir, 5.0f));
        }

        if (hit) {
            spawn_hit_particles(data, hit_point, vec3_create(0.95f, 0.9f, 0.8f), vec3_normalize(proj->velocity), 2);
            proj->active = false;
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

    particle_system_update(data->particles, dt);
    voxel_object_world_update(data->vobj_world, dt);
    spawn_props_near_player(data, data->player.position, floor_y);

    data->spawn_timer += dt;
    float spawn_rate = data->spawn_interval / (1.0f + data->difficulty * 0.1f);
    if (data->spawn_timer >= spawn_rate && data->enemy_count < SHOOTER_MAX_ENEMIES) {
        spawn_enemy(data, &scene->bounds);
        data->spawn_timer = 0.0f;
        data->difficulty += 0.1f;
    }
}

static void shooter_handle_input(Scene* scene, float mouse_x, float mouse_y, bool left_down, bool right_down) {
    (void)scene;
    (void)mouse_x;
    (void)mouse_y;
    (void)left_down;
    (void)right_down;
}

static const char* shooter_get_name(Scene* scene) {
    (void)scene;
    return "Shooter";
}

static const SceneVTable shooter_vtable = {
    .init = shooter_init,
    .destroy = shooter_destroy_impl,
    .update = shooter_update,
    .handle_input = shooter_handle_input,
    .render = NULL,
    .get_name = shooter_get_name
};

Scene* shooter_scene_create(Bounds3D bounds) {
    Scene* scene = (Scene*)calloc(1, sizeof(Scene));
    if (!scene) return NULL;

    ShooterData* data = (ShooterData*)calloc(1, sizeof(ShooterData));
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
    data->destroyed_cubes = 0;
    data->spawn_timer = 0.0f;
    data->spawn_interval = 1.5f;
    data->difficulty = 1.0f;
    data->shoot_cooldown = 0.0f;

    data->aiming = false;

    data->aim_origin = vec3_zero();
    data->aim_dir = vec3_create(0.0f, 0.0f, 1.0f);
    data->aim_valid = false;

    memset(&data->input, 0, sizeof(PlayerInput));
    for (int32_t i = 0; i < SHOOTER_MAX_PROJECTILES; i++) {
        data->projectiles[i].active = false;
    }

    scene->vtable = &shooter_vtable;
    scene->bounds = bounds;
    scene->user_data = data;

    return scene;
}

void shooter_set_input(Scene* scene, bool w, bool a, bool s, bool d, bool left_click, bool right_down) {
    if (!scene || !scene->user_data) return;
    ShooterData* data = (ShooterData*)scene->user_data;
    data->input.move_forward = w;
    data->input.move_left = a;
    data->input.move_backward = s;
    data->input.move_right = d;
    data->input.punch = left_click;
    data->input.grab = false;

    data->aiming = right_down;
}

void shooter_set_aim_ray(Scene* scene, Vec3 origin, Vec3 dir) {
    if (!scene || !scene->user_data) return;
    ShooterData* data = (ShooterData*)scene->user_data;
    data->aim_origin = origin;
    data->aim_dir = vec3_normalize(dir);
    data->aim_valid = true;
}

ShooterData* shooter_get_data(Scene* scene) {
    if (!scene) return NULL;
    return (ShooterData*)scene->user_data;
}
