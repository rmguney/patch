#include "player.h"
#include "combat.h"
#include <math.h>

void player_init(Player* player, Vec3 position) {
    player->position = position;
    player->velocity = vec3_zero();
    player->yaw = 0.0f;
    
    player->body_width = 0.4f;
    player->body_height = 0.6f;
    player->body_depth = 0.2f;
    
    player->head_size = 0.4f;
    player->arm_width = 0.15f;
    player->arm_length = 0.5f;
    player->leg_width = 0.15f;
    player->leg_length = 0.5f;
    
    HumanoidDimensions dims;
    dims.body_width = player->body_width;
    dims.body_height = player->body_height;
    dims.body_depth = player->body_depth;
    dims.head_size = player->head_size;
    dims.arm_width = player->arm_width;
    dims.arm_length = player->arm_length;
    dims.leg_width = player->leg_width;
    dims.leg_length = player->leg_length;
    
    humanoid_model_init(&player->model, &dims);
    humanoid_model_build_voxels(&player->model);
    
    player->move_speed = 5.0f;
    player->punch_cooldown = PLAYER_PUNCH_COOLDOWN;
    player->punch_timer = 0.0f;
    player->is_punching = false;
    
    player->held_enemy_id = -1;
    player->is_holding = false;
    
    player->arm_swing = 0.0f;
    player->leg_swing = 0.0f;
    player->walk_cycle = 0.0f;
    
    player->is_dead = false;
}

void player_update(Player* player, const PlayerInput* input, float dt) {
    if (player->is_dead) {
        player->velocity.x *= 0.9f;
        player->velocity.z *= 0.9f;
        player->arm_swing *= 0.9f;
        player->leg_swing *= 0.9f;
        return;
    }
    
    Vec3 move_dir = vec3_zero();
    
    float iso_angle = 45.0f * K_DEG_TO_RAD;
    float cos_iso = cosf(iso_angle);
    float sin_iso = sinf(iso_angle);
    
    if (input->move_forward) move_dir.x -= 1.0f;
    if (input->move_backward) move_dir.x += 1.0f;
    if (input->move_left) move_dir.z -= 1.0f;
    if (input->move_right) move_dir.z += 1.0f;
    
    float move_len = vec3_length(move_dir);
    bool is_moving = move_len > 0.01f;
    
    if (is_moving) {
        move_dir = vec3_scale(move_dir, 1.0f / move_len);
        
        float iso_x = move_dir.x * cos_iso + move_dir.z * sin_iso;
        float iso_z = move_dir.x * sin_iso - move_dir.z * cos_iso;
        
        player->velocity.x = iso_x * player->move_speed;
        player->velocity.z = iso_z * player->move_speed;
        player->yaw = atan2f(-move_dir.x, -move_dir.z) + iso_angle;
        
        player->walk_cycle += dt * 10.0f;
        float walk_anim = sinf(player->walk_cycle);
        player->leg_swing = walk_anim * 0.8f;
        player->arm_swing = -walk_anim * 0.6f;
    } else {
        player->velocity.x *= 0.8f;
        player->velocity.z *= 0.8f;
        player->leg_swing *= 0.85f;
        player->arm_swing *= 0.85f;
    }
    
    player->position = vec3_add(player->position, vec3_scale(player->velocity, dt));
    
    if (player->punch_timer > 0.0f) {
        player->punch_timer -= dt;
        if (player->punch_timer <= 0.0f) player->is_punching = false;
    }
    
    if (input->punch && player_can_punch(player)) player_start_punch(player);
}

Vec3 player_get_right_shoulder(const Player* player) {
    return humanoid_get_shoulder(player->position, &player->model.dims, player->yaw, true);
}

Vec3 player_get_right_hand(const Player* player) {
    Vec3 forward = humanoid_get_forward(player->yaw);
    Vec3 shoulder = player_get_right_shoulder(player);
    
    float punch_extend = 0.0f;
    if (player->is_punching) {
        float t = player->punch_timer / player->punch_cooldown;
        punch_extend = sinf((1.0f - t) * K_PI) * 0.3f;
    }
    
    float reach = player->arm_length + punch_extend;
    Vec3 hand = vec3_add(shoulder, vec3_scale(forward, reach));
    hand.y = shoulder.y - player->arm_length * 0.5f;
    return hand;
}

CapsuleHitbox player_get_punch_hitbox(const Player* player) {
    Vec3 shoulder = humanoid_get_shoulder(player->position, &player->model.dims, player->yaw, true);
    Vec3 forward = humanoid_get_forward(player->yaw);
    return combat_get_punch_hitbox(shoulder, forward, player->arm_length);
}

CapsuleHitbox player_get_grab_hitbox(const Player* player) {
    Vec3 shoulder = humanoid_get_shoulder(player->position, &player->model.dims, player->yaw, true);
    Vec3 forward = humanoid_get_forward(player->yaw);
    return combat_get_grab_hitbox(shoulder, forward, player->arm_length);
}

Vec3 player_get_head_position(const Player* player) {
    return humanoid_get_head_center(player->position, &player->model.dims);
}

Vec3 player_get_body_center(const Player* player) {
    return humanoid_get_body_center(player->position, &player->model.dims);
}

float player_get_collision_radius(const Player* player) {
    return humanoid_get_collision_radius(&player->model.dims);
}

void player_start_punch(Player* player) {
    player->is_punching = true;
    player->punch_timer = player->punch_cooldown;
}

bool player_can_punch(const Player* player) {
    return !player->is_punching && player->punch_timer <= 0.0f;
}

int32_t player_damage_at_point(Player* player, Vec3 hit_point, float damage, Vec3 hit_direction,
    Vec3* out_positions, Vec3* out_colors, int32_t max_out) {
    if (player->is_dead) return 0;
    
    HumanoidPose pose = player_get_pose(player);
    Vec3 player_color = vec3_create(0.20f, 0.60f, 0.85f);
    int32_t destroyed = 0;
    
    humanoid_damage_at_point(&player->model, player->position, &pose, hit_point,
                              damage, hit_direction, player_color, 
                              out_positions, out_colors, max_out, 1, &destroyed);
    return destroyed;
}

float player_get_mass_ratio(const Player* player) {
    return humanoid_get_mass_ratio(&player->model);
}

HumanoidPose player_get_pose(const Player* player) {
    float punch_swing = humanoid_calculate_punch_swing(player->is_punching, player->punch_timer,
                                                        player->punch_cooldown, 1.8f);
    return humanoid_make_pose(player->yaw, player->arm_swing, player->leg_swing, punch_swing);
}
