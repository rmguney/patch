#include "enemy.h"
#include "combat.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

static float randf_range(float min_val, float max_val) {
    return min_val + randf() * (max_val - min_val);
}

void enemy_init(Enemy* enemy, Vec3 position, int32_t id) {
    memset(enemy, 0, sizeof(Enemy));
    
    enemy->position = position;
    enemy->velocity = vec3_zero();
    enemy->yaw = randf_range(0.0f, 6.28f);
    enemy->target_yaw = enemy->yaw;
    enemy->steering = vec3_zero();
    
    HumanoidDimensions dims;
    dims.body_width = 0.35f;
    dims.body_height = 0.5f;
    dims.body_depth = 0.18f;
    dims.head_size = 0.35f;
    dims.arm_width = 0.12f;
    dims.arm_length = 0.45f;
    dims.leg_width = 0.12f;
    dims.leg_length = 0.45f;
    
    humanoid_model_init(&enemy->model, &dims);
    humanoid_model_build_voxels(&enemy->model);
    
    enemy->state = ENEMY_STATE_CHASE;
    enemy->state_timer = 0.0f;
    enemy->attack_cooldown = randf_range(0.2f, 0.8f);
    enemy->aggro_timer = 0.0f;
    
    enemy->walk_cycle = randf_range(0.0f, 6.28f);
    enemy->arm_swing = 0.0f;
    enemy->leg_swing = 0.0f;
    
    enemy->active = true;
    enemy->id = id;
    
    enemy->hit_this_punch = false;
    enemy->hit_this_attack = false;
    enemy->attack_height_offset = randf_range(-0.3f, 0.4f);
    enemy->attack_side_offset = randf_range(-0.15f, 0.15f);
}

void enemy_update(Enemy* enemy, Vec3 player_pos, float dt) {
    if (!enemy->active) return;
    if (enemy->state == ENEMY_STATE_DYING || enemy->state == ENEMY_STATE_DEAD) return;
    
    if (!humanoid_head_connected(&enemy->model)) {
        enemy_start_dying(enemy);
        return;
    }
    
    float mass_ratio = humanoid_get_mass_ratio(&enemy->model);
    if (mass_ratio < ENEMY_DEATH_MASS_RATIO) {
        enemy_start_dying(enemy);
        return;
    }
    
    Vec3 to_player = vec3_sub(player_pos, enemy->position);
    to_player.y = 0.0f;
    float dist = vec3_length(to_player);
    Vec3 dir_to_player = dist > 0.001f ? vec3_scale(to_player, 1.0f / dist) : vec3_create(0.0f, 0.0f, 1.0f);
    
    enemy->state_timer -= dt;
    if (enemy->attack_cooldown > 0.0f) enemy->attack_cooldown -= dt;
    enemy->aggro_timer += dt;
    
    float max_speed = 3.2f;
    float accel = 15.0f;
    Vec3 desired_vel = vec3_zero();
    bool wants_attack = false;
    
    switch (enemy->state) {
        case ENEMY_STATE_IDLE:
        case ENEMY_STATE_WANDER:
            enemy->state = ENEMY_STATE_CHASE;
            break;
            
        case ENEMY_STATE_CHASE: {
            float engage_dist = 0.9f;
            
            if (dist > engage_dist) {
                desired_vel = vec3_scale(dir_to_player, max_speed);
            } else {
                desired_vel = vec3_scale(dir_to_player, max_speed * 0.7f);
                wants_attack = true;
            }
            
            if (wants_attack && enemy->attack_cooldown <= 0.0f) {
                enemy->state = ENEMY_STATE_ATTACK;
                enemy->state_timer = ENEMY_ATTACK_DURATION;
                enemy->attack_cooldown = ENEMY_PUNCH_COOLDOWN + randf_range(0.0f, 0.15f);
                enemy->hit_this_attack = false;
            }
            break;
        }
            
        case ENEMY_STATE_ATTACK:
            desired_vel = vec3_scale(dir_to_player, max_speed * 0.6f);
            if (enemy->state_timer <= 0.0f) {
                enemy->state = ENEMY_STATE_CHASE;
            }
            break;
            
        case ENEMY_STATE_STAGGER:
            desired_vel = vec3_zero();
            if (enemy->state_timer <= 0.0f) {
                enemy->state = ENEMY_STATE_CHASE;
            }
            break;
            
        case ENEMY_STATE_HELD:
        case ENEMY_STATE_DYING:
        case ENEMY_STATE_DEAD:
            return;
    }
    
    desired_vel.x += enemy->steering.x * 3.0f;
    desired_vel.z += enemy->steering.z * 3.0f;
    
    Vec3 vel_diff = vec3_sub(desired_vel, enemy->velocity);
    vel_diff.y = 0.0f;
    float diff_len = vec3_length(vel_diff);
    if (diff_len > 0.001f) {
        float apply = fminf(accel * dt, diff_len);
        Vec3 accel_dir = vec3_scale(vel_diff, apply / diff_len);
        enemy->velocity = vec3_add(enemy->velocity, accel_dir);
    }
    
    float speed = sqrtf(enemy->velocity.x * enemy->velocity.x + enemy->velocity.z * enemy->velocity.z);
    if (speed > max_speed) {
        enemy->velocity.x = (enemy->velocity.x / speed) * max_speed;
        enemy->velocity.z = (enemy->velocity.z / speed) * max_speed;
    }
    
    enemy->position = vec3_add(enemy->position, vec3_scale(enemy->velocity, dt));
    
    enemy->target_yaw = atan2f(-dir_to_player.x, dir_to_player.z);
    enemy->yaw = lerp_angle(enemy->yaw, enemy->target_yaw, 12.0f * dt);
    
    if (speed > 0.5f) {
        enemy->walk_cycle += dt * speed * 4.0f;
        float walk_anim = sinf(enemy->walk_cycle);
        enemy->leg_swing = walk_anim * 0.6f;
        enemy->arm_swing = -walk_anim * 0.4f;
    } else {
        enemy->leg_swing *= 0.9f;
        enemy->arm_swing *= 0.9f;
    }
}

Vec3 enemy_get_right_shoulder(const Enemy* enemy) {
    return humanoid_get_shoulder(enemy->position, &enemy->model.dims, enemy->yaw, true);
}

CapsuleHitbox enemy_get_punch_hitbox(const Enemy* enemy) {
    Vec3 shoulder = humanoid_get_shoulder(enemy->position, &enemy->model.dims, enemy->yaw, true);
    Vec3 forward = humanoid_get_forward(enemy->yaw);
    CapsuleHitbox hitbox = combat_get_punch_hitbox(shoulder, forward, enemy->model.dims.arm_length);

    hitbox.radius *= 1.35f;
    hitbox.start = vec3_sub(hitbox.start, vec3_scale(forward, 0.10f));
    hitbox.end = vec3_add(hitbox.end, vec3_scale(forward, 0.25f));

    return hitbox;
}

Vec3 enemy_get_head_position(const Enemy* enemy) {
    return humanoid_get_head_center(enemy->position, &enemy->model.dims);
}

Vec3 enemy_get_body_center(const Enemy* enemy) {
    return humanoid_get_body_center(enemy->position, &enemy->model.dims);
}

float enemy_get_collision_radius(const Enemy* enemy) {
    return humanoid_get_collision_radius(&enemy->model.dims);
}

int32_t enemy_damage_at_point(Enemy* enemy, Vec3 hit_point, float damage, Vec3 hit_direction,
    Vec3* out_positions, Vec3* out_colors, int32_t max_out) {
    if (!enemy->active) return 0;
    if (enemy->state == ENEMY_STATE_DYING || enemy->state == ENEMY_STATE_DEAD) return 0;
    
    HumanoidPose pose = enemy_get_pose(enemy);
    Vec3 enemy_color = vec3_create(0.85f, 0.45f, 0.45f);
    int32_t destroyed_count = 0;
    
    humanoid_damage_at_point(&enemy->model, enemy->position, &pose, hit_point, damage, 
                              hit_direction, enemy_color, out_positions, out_colors, 
                              max_out, 5, &destroyed_count);
    
    if (damage > 15.0f && destroyed_count > 0) {
        enemy->state = ENEMY_STATE_STAGGER;
        enemy->state_timer = 0.3f;
    }
    
    return destroyed_count;
}

void enemy_reset_punch_state(Enemy* enemy) {
    enemy->hit_this_punch = false;
}

float enemy_get_mass_ratio(const Enemy* enemy) {
    return humanoid_get_mass_ratio(&enemy->model);
}

bool enemy_is_dead(const Enemy* enemy) {
    return enemy->state == ENEMY_STATE_DEAD || enemy->state == ENEMY_STATE_DYING;
}

void enemy_start_dying(Enemy* enemy) {
    if (enemy->state == ENEMY_STATE_DYING || enemy->state == ENEMY_STATE_DEAD) return;
    
    enemy->state = ENEMY_STATE_DYING;
    enemy->collapse_progress = 0.0f;
    
    humanoid_start_ragdoll(&enemy->model, enemy->position, enemy->velocity, enemy->model.last_hit_direction);
}

void enemy_update_death(Enemy* enemy, float floor_y, float dt) {
    if (enemy->state != ENEMY_STATE_DYING && enemy->state != ENEMY_STATE_DEAD) return;

    if (!enemy->model.ragdoll.ragdoll_active) {
        humanoid_start_ragdoll(&enemy->model, enemy->position, enemy->velocity, enemy->model.last_hit_direction);
    }
    
    humanoid_update_ragdoll(&enemy->model, floor_y, dt);
    
    enemy->position = enemy->model.ragdoll.position;
    enemy->yaw += enemy->model.ragdoll.angular_velocity.y * dt;
    
    float speed = vec3_length(enemy->model.ragdoll.velocity);
    float ang_speed = vec3_length(enemy->model.ragdoll.angular_velocity);
    
    if (speed < 0.1f && ang_speed < 0.3f && enemy->state == ENEMY_STATE_DYING) {
        enemy->collapse_progress += dt * 3.0f;
        if (enemy->collapse_progress >= 1.0f) {
            enemy->collapse_progress = 1.0f;
            enemy->state = ENEMY_STATE_DEAD;
        }
    }
}

bool enemy_update_held(Enemy* enemy, Vec3 hold_pos, Vec3 holder_velocity, float dt) {
    if (enemy->state != ENEMY_STATE_HELD) {
        enemy->state = ENEMY_STATE_HELD;
    }
    
    if (humanoid_should_die(&enemy->model)) {
        enemy->state = ENEMY_STATE_DYING;
        enemy->collapse_progress = 0.0f;
        humanoid_start_ragdoll(&enemy->model, enemy->position, enemy->velocity, enemy->model.last_hit_direction);
        return true;
    }
    
    enemy->position = hold_pos;
    enemy->velocity = holder_velocity;
    
    enemy->arm_swing *= 0.9f;
    enemy->leg_swing *= 0.9f;
    
    (void)dt;
    return false;
}

HumanoidPose enemy_get_pose(const Enemy* enemy) {
    float punch_swing = humanoid_calculate_punch_swing(
        enemy->state == ENEMY_STATE_ATTACK, enemy->state_timer, 0.4f, 1.5f);
    return humanoid_make_pose(enemy->yaw, enemy->arm_swing, enemy->leg_swing, punch_swing);
}
