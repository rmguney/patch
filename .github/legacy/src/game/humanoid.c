#include "humanoid.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static void add_voxel(HumanoidModel* model, float x, float y, float z, HumanoidPart part, float mass) {
    if (model->voxel_count >= HUMANOID_MAX_VOXELS) return;
    
    HumanoidVoxel* v = &model->voxels[model->voxel_count++];
    v->local_offset = vec3_create(x, y, z);
    v->part = part;
    v->mass = mass;
    v->active = true;
    
    model->total_mass += mass;
    model->current_mass += mass;
}

void humanoid_model_init(HumanoidModel* model, const HumanoidDimensions* dims) {
    memset(model, 0, sizeof(HumanoidModel));
    model->dims = *dims;
    model->ragdoll.ragdoll_active = false;
    model->ragdoll.ragdoll_time = 0.0f;
    model->last_hit_direction = vec3_zero();
}

void humanoid_model_build_voxels(HumanoidModel* model) {
    model->voxel_count = 0;
    model->total_mass = 0.0f;
    model->current_mass = 0.0f;
    
    const HumanoidDimensions* d = &model->dims;
    float vs = HUMANOID_VOXEL_SIZE;
    
    int head_nx = (int)ceilf(d->head_size / vs);
    int head_ny = (int)ceilf(d->head_size / vs);
    int head_nz = (int)ceilf(d->head_size / vs);
    float head_start_x = -d->head_size * 0.5f + vs * 0.5f;
    float head_start_z = -d->head_size * 0.5f + vs * 0.5f;
    
    for (int ix = 0; ix < head_nx; ix++) {
        for (int iy = 0; iy < head_ny; iy++) {
            for (int iz = 0; iz < head_nz; iz++) {
                float x = head_start_x + ix * vs;
                float y = iy * vs;
                float z = head_start_z + iz * vs;
                add_voxel(model, x, y, z, HUMANOID_PART_HEAD, 2.0f);
            }
        }
    }
    
    int body_nx = (int)ceilf(d->body_width / vs);
    int body_ny = (int)ceilf(d->body_height / vs);
    int body_nz = (int)ceilf(d->body_depth / vs);
    float body_start_x = -d->body_width * 0.5f + vs * 0.5f;
    float body_start_z = -d->body_depth * 0.5f + vs * 0.5f;
    
    for (int ix = 0; ix < body_nx; ix++) {
        for (int iy = 0; iy < body_ny; iy++) {
            for (int iz = 0; iz < body_nz; iz++) {
                float x = body_start_x + ix * vs;
                float y = iy * vs;
                float z = body_start_z + iz * vs;
                add_voxel(model, x, y, z, HUMANOID_PART_BODY, 1.5f);
            }
        }
    }
    
    int arm_nx = (int)ceilf(d->arm_width / vs);
    int arm_ny = (int)ceilf(d->arm_length / vs);
    float arm_start_x = -d->arm_width * 0.5f + vs * 0.5f;
    float arm_start_z = -d->arm_width * 0.5f + vs * 0.5f;
    
    for (int ix = 0; ix < arm_nx; ix++) {
        for (int iy = 0; iy < arm_ny; iy++) {
            for (int iz = 0; iz < arm_nx; iz++) {
                float x = arm_start_x + ix * vs;
                float y = -iy * vs;
                float z = arm_start_z + iz * vs;
                add_voxel(model, x, y, z, HUMANOID_PART_ARM_LEFT, 0.8f);
                add_voxel(model, x, y, z, HUMANOID_PART_ARM_RIGHT, 0.8f);
            }
        }
    }
    
    int leg_nx = (int)ceilf(d->leg_width / vs);
    int leg_ny = (int)ceilf(d->leg_length / vs);
    float leg_start_x = -d->leg_width * 0.5f + vs * 0.5f;
    float leg_start_z = -d->leg_width * 0.5f + vs * 0.5f;
    
    for (int ix = 0; ix < leg_nx; ix++) {
        for (int iy = 0; iy < leg_ny; iy++) {
            for (int iz = 0; iz < leg_nx; iz++) {
                float x = leg_start_x + ix * vs;
                float y = -iy * vs;
                float z = leg_start_z + iz * vs;
                add_voxel(model, x, y, z, HUMANOID_PART_LEG_LEFT, 1.0f);
                add_voxel(model, x, y, z, HUMANOID_PART_LEG_RIGHT, 1.0f);
            }
        }
    }
    
    model->center_of_mass_offset = humanoid_calculate_center_of_mass(model);
}

Vec3 humanoid_transform_voxel(const HumanoidVoxel* voxel, Vec3 base_pos, 
                               const HumanoidDimensions* dims, const HumanoidPose* pose) {
    float sin_yaw = sinf(pose->yaw);
    float cos_yaw = cosf(pose->yaw);
    
    Vec3 local = voxel->local_offset;
    Vec3 pivot = vec3_zero();
    Vec3 result;
    
    switch (voxel->part) {
        case HUMANOID_PART_HEAD: {
            pivot.y = dims->leg_length + dims->body_height;
            Vec3 rotated;
            rotated.x = local.x * cos_yaw - local.z * sin_yaw;
            rotated.y = local.y;
            rotated.z = local.x * sin_yaw + local.z * cos_yaw;
            result = vec3_add(base_pos, vec3_add(pivot, rotated));
            break;
        }
        
        case HUMANOID_PART_BODY: {
            pivot.y = dims->leg_length;
            Vec3 rotated;
            rotated.x = local.x * cos_yaw - local.z * sin_yaw;
            rotated.y = local.y;
            rotated.z = local.x * sin_yaw + local.z * cos_yaw;
            result = vec3_add(base_pos, vec3_add(pivot, rotated));
            break;
        }
        
        case HUMANOID_PART_ARM_LEFT: {
            float shoulder_y = dims->leg_length + dims->body_height * 0.85f;
            float arm_offset = dims->body_width * 0.5f + dims->arm_width * 0.5f;
            
            pivot.x = -arm_offset;
            pivot.y = shoulder_y;
            
            float swing = pose->arm_swing;
            float sin_swing = sinf(swing);
            float cos_swing = cosf(swing);
            
            Vec3 swung;
            swung.x = local.x;
            swung.y = local.y * cos_swing - local.z * sin_swing;
            swung.z = local.y * sin_swing + local.z * cos_swing;
            
            Vec3 world_pivot;
            world_pivot.x = pivot.x * cos_yaw - pivot.z * sin_yaw;
            world_pivot.y = pivot.y;
            world_pivot.z = pivot.x * sin_yaw + pivot.z * cos_yaw;
            
            Vec3 rotated;
            rotated.x = swung.x * cos_yaw - swung.z * sin_yaw;
            rotated.y = swung.y;
            rotated.z = swung.x * sin_yaw + swung.z * cos_yaw;
            
            result = vec3_add(base_pos, vec3_add(world_pivot, rotated));
            break;
        }
        
        case HUMANOID_PART_ARM_RIGHT: {
            float shoulder_y = dims->leg_length + dims->body_height * 0.85f;
            float arm_offset = dims->body_width * 0.5f + dims->arm_width * 0.5f;
            
            pivot.x = arm_offset;
            pivot.y = shoulder_y;
            
            float swing = -pose->arm_swing - pose->punch_swing;
            float sin_swing = sinf(swing);
            float cos_swing = cosf(swing);
            
            Vec3 swung;
            swung.x = local.x;
            swung.y = local.y * cos_swing - local.z * sin_swing;
            swung.z = local.y * sin_swing + local.z * cos_swing;
            
            Vec3 world_pivot;
            world_pivot.x = pivot.x * cos_yaw - pivot.z * sin_yaw;
            world_pivot.y = pivot.y;
            world_pivot.z = pivot.x * sin_yaw + pivot.z * cos_yaw;
            
            Vec3 rotated;
            rotated.x = swung.x * cos_yaw - swung.z * sin_yaw;
            rotated.y = swung.y;
            rotated.z = swung.x * sin_yaw + swung.z * cos_yaw;
            
            result = vec3_add(base_pos, vec3_add(world_pivot, rotated));
            break;
        }
        
        case HUMANOID_PART_LEG_LEFT: {
            float leg_offset = dims->body_width * 0.3f;
            pivot.x = -leg_offset;
            pivot.y = dims->leg_length;
            
            float swing = pose->leg_swing;
            float sin_swing = sinf(swing);
            float cos_swing = cosf(swing);
            
            Vec3 swung;
            swung.x = local.x;
            swung.y = local.y * cos_swing - local.z * sin_swing;
            swung.z = local.y * sin_swing + local.z * cos_swing;
            
            Vec3 world_pivot;
            world_pivot.x = pivot.x * cos_yaw - pivot.z * sin_yaw;
            world_pivot.y = pivot.y;
            world_pivot.z = pivot.x * sin_yaw + pivot.z * cos_yaw;
            
            Vec3 rotated;
            rotated.x = swung.x * cos_yaw - swung.z * sin_yaw;
            rotated.y = swung.y;
            rotated.z = swung.x * sin_yaw + swung.z * cos_yaw;
            
            result = vec3_add(base_pos, vec3_add(world_pivot, rotated));
            break;
        }
        
        case HUMANOID_PART_LEG_RIGHT: {
            float leg_offset = dims->body_width * 0.3f;
            pivot.x = leg_offset;
            pivot.y = dims->leg_length;
            
            float swing = -pose->leg_swing;
            float sin_swing = sinf(swing);
            float cos_swing = cosf(swing);
            
            Vec3 swung;
            swung.x = local.x;
            swung.y = local.y * cos_swing - local.z * sin_swing;
            swung.z = local.y * sin_swing + local.z * cos_swing;
            
            Vec3 world_pivot;
            world_pivot.x = pivot.x * cos_yaw - pivot.z * sin_yaw;
            world_pivot.y = pivot.y;
            world_pivot.z = pivot.x * sin_yaw + pivot.z * cos_yaw;
            
            Vec3 rotated;
            rotated.x = swung.x * cos_yaw - swung.z * sin_yaw;
            rotated.y = swung.y;
            rotated.z = swung.x * sin_yaw + swung.z * cos_yaw;
            
            result = vec3_add(base_pos, vec3_add(world_pivot, rotated));
            break;
        }
    }
    
    return result;
}

Vec3 humanoid_get_voxel_rotation(const HumanoidVoxel* voxel, const HumanoidPose* pose) {
    float swing = 0.0f;
    
    switch (voxel->part) {
        case HUMANOID_PART_ARM_LEFT:
            swing = pose->arm_swing;
            break;
        case HUMANOID_PART_ARM_RIGHT:
            swing = -pose->arm_swing - pose->punch_swing;
            break;
        case HUMANOID_PART_LEG_LEFT:
            swing = pose->leg_swing;
            break;
        case HUMANOID_PART_LEG_RIGHT:
            swing = -pose->leg_swing;
            break;
        default:
            break;
    }
    
    return vec3_create(swing, pose->yaw, 0.0f);
}

static Vec3 get_part_color(HumanoidPart part, Vec3 base_color) {
    switch (part) {
        case HUMANOID_PART_HEAD:
            return vec3_create(0.95f, 0.70f, 0.65f);
        case HUMANOID_PART_BODY:
            return base_color;
        case HUMANOID_PART_ARM_LEFT:
        case HUMANOID_PART_ARM_RIGHT:
            return vec3_create(0.95f, 0.70f, 0.65f);
        case HUMANOID_PART_LEG_LEFT:
        case HUMANOID_PART_LEG_RIGHT:
            return vec3_create(base_color.x * 0.85f, base_color.y * 0.85f, base_color.z * 0.85f);
        default:
            return base_color;
    }
}

bool humanoid_damage_at_point(HumanoidModel* model, Vec3 base_pos, const HumanoidPose* pose,
                               Vec3 world_hit, float damage, Vec3 hit_direction, Vec3 base_color,
                               Vec3* out_positions, Vec3* out_colors, int32_t max_out, int32_t max_destroy, int32_t* out_count) {
    float damage_radius = 0.25f;
    int32_t destroyed_count = 0;
    
    model->last_hit_direction = hit_direction;
    
    float closest_dist = damage_radius;
    int closest_idx = -1;
    
    for (int i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active) continue;
        
        Vec3 voxel_world_pos = humanoid_transform_voxel(&model->voxels[i], base_pos, &model->dims, pose);
        float dist = vec3_length(vec3_sub(world_hit, voxel_world_pos));
        
        if (dist < closest_dist) {
            closest_dist = dist;
            closest_idx = i;
        }
    }
    
    while (closest_idx >= 0 && destroyed_count < max_out && destroyed_count < max_destroy) {
        HumanoidVoxel* voxel = &model->voxels[closest_idx];
        Vec3 voxel_world_pos = humanoid_transform_voxel(voxel, base_pos, &model->dims, pose);
        
        model->current_mass -= voxel->mass;
        voxel->active = false;
        out_positions[destroyed_count] = voxel_world_pos;
        out_colors[destroyed_count] = get_part_color(voxel->part, base_color);
        destroyed_count++;
        
        closest_idx = -1;
        closest_dist = damage_radius;
        if (destroyed_count < max_destroy) {
            for (int i = 0; i < model->voxel_count; i++) {
                if (!model->voxels[i].active) continue;
                Vec3 pos = humanoid_transform_voxel(&model->voxels[i], base_pos, &model->dims, pose);
                float dist = vec3_length(vec3_sub(world_hit, pos));
                if (dist < closest_dist) {
                    closest_dist = dist;
                    closest_idx = i;
                }
            }
        }
    }
    
    *out_count = destroyed_count;
    return destroyed_count > 0;
}

float humanoid_get_mass_ratio(const HumanoidModel* model) {
    if (model->total_mass < 0.001f) return 0.0f;
    return model->current_mass / model->total_mass;
}

Vec3 humanoid_calculate_center_of_mass(const HumanoidModel* model) {
    Vec3 com = vec3_zero();
    float total = 0.0f;
    
    HumanoidPose neutral_pose = {0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 base = vec3_zero();
    
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active) continue;
        Vec3 pos = humanoid_transform_voxel(&model->voxels[i], base, &model->dims, &neutral_pose);
        com = vec3_add(com, vec3_scale(pos, model->voxels[i].mass));
        total += model->voxels[i].mass;
    }
    
    if (total > 0.001f) {
        com = vec3_scale(com, 1.0f / total);
    }
    return com;
}

void humanoid_start_ragdoll(HumanoidModel* model, Vec3 position, Vec3 velocity, Vec3 hit_direction) {
    if (vec3_length(hit_direction) < 0.2f) {
        hit_direction = vec3_create(0.35f, 1.0f, 0.25f);
    }
    hit_direction = vec3_normalize(hit_direction);
    model->ragdoll.ragdoll_active = true;
    model->ragdoll.ragdoll_time = 0.0f;
    model->ragdoll.position = position;
    
    Vec3 push = vec3_scale(hit_direction, 7.0f);
    push.y = 4.5f;
    model->ragdoll.velocity = vec3_add(velocity, push);
    
    model->ragdoll.rotation = vec3_zero();
    
    float torque_strength = 10.0f;
    model->ragdoll.angular_velocity = vec3_create(
        hit_direction.z * torque_strength,
        0.0f,
        -hit_direction.x * torque_strength
    );
    
    model->ragdoll.torso.position = vec3_zero();
    model->ragdoll.torso.velocity = vec3_zero();
    model->ragdoll.torso.rotation = vec3_zero();
    model->ragdoll.torso.angular_velocity = model->ragdoll.angular_velocity;
    
    model->ragdoll.head.position = vec3_create(0.0f, model->dims.body_height, 0.0f);
    model->ragdoll.head.velocity = vec3_create(hit_direction.x * 2.0f, 1.0f, hit_direction.z * 2.0f);
    model->ragdoll.head.rotation = vec3_zero();
    model->ragdoll.head.angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 4.0f,
        ((float)rand() / RAND_MAX - 0.5f) * 2.0f,
        ((float)rand() / RAND_MAX - 0.5f) * 4.0f
    );
    
    float side_spread = 3.0f;
    model->ragdoll.arm_left.position = vec3_create(-model->dims.body_width * 0.5f, model->dims.body_height * 0.8f, 0.0f);
    model->ragdoll.arm_left.velocity = vec3_create(-side_spread, 1.0f, 0.0f);
    model->ragdoll.arm_left.rotation = vec3_create(0.0f, 0.0f, -0.5f);
    model->ragdoll.arm_left.angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 6.0f,
        0.0f,
        ((float)rand() / RAND_MAX) * -4.0f
    );
    
    model->ragdoll.arm_right.position = vec3_create(model->dims.body_width * 0.5f, model->dims.body_height * 0.8f, 0.0f);
    model->ragdoll.arm_right.velocity = vec3_create(side_spread, 1.0f, 0.0f);
    model->ragdoll.arm_right.rotation = vec3_create(0.0f, 0.0f, 0.5f);
    model->ragdoll.arm_right.angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 6.0f,
        0.0f,
        ((float)rand() / RAND_MAX) * 4.0f
    );
    
    model->ragdoll.leg_left.position = vec3_create(-model->dims.body_width * 0.3f, 0.0f, 0.0f);
    model->ragdoll.leg_left.velocity = vec3_create(-side_spread * 0.5f, 0.0f, hit_direction.z);
    model->ragdoll.leg_left.rotation = vec3_zero();
    model->ragdoll.leg_left.angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 4.0f,
        0.0f,
        ((float)rand() / RAND_MAX) * -2.0f
    );
    
    model->ragdoll.leg_right.position = vec3_create(model->dims.body_width * 0.3f, 0.0f, 0.0f);
    model->ragdoll.leg_right.velocity = vec3_create(side_spread * 0.5f, 0.0f, hit_direction.z);
    model->ragdoll.leg_right.rotation = vec3_zero();
    model->ragdoll.leg_right.angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 4.0f,
        0.0f,
        ((float)rand() / RAND_MAX) * 2.0f
    );
}

static void update_ragdoll_limb(RagdollLimb* limb, Vec3 anchor, float constraint_dist, float floor_y, float dt) {
    float gravity = -25.0f;
    float damping = 0.96f;
    float angular_damping = 0.92f;
    float bounce = 0.3f;
    
    limb->velocity.y += gravity * dt;
    limb->position = vec3_add(limb->position, vec3_scale(limb->velocity, dt));
    limb->rotation = vec3_add(limb->rotation, vec3_scale(limb->angular_velocity, dt));
    
    Vec3 to_anchor = vec3_sub(anchor, limb->position);
    float dist = vec3_length(to_anchor);
    if (dist > constraint_dist && dist > 0.001f) {
        float correction = (dist - constraint_dist) * 0.5f;
        Vec3 dir = vec3_scale(to_anchor, 1.0f / dist);
        limb->position = vec3_add(limb->position, vec3_scale(dir, correction));
        
        float vel_along = vec3_dot(limb->velocity, dir);
        if (vel_along < 0.0f) {
            limb->velocity = vec3_add(limb->velocity, vec3_scale(dir, -vel_along * 0.8f));
        }
    }
    
    if (limb->position.y < floor_y) {
        limb->position.y = floor_y;
        if (limb->velocity.y < -0.5f) {
            limb->velocity.y = -limb->velocity.y * bounce;
            limb->angular_velocity = vec3_scale(limb->angular_velocity, 0.7f);
        } else {
            limb->velocity.y = 0.0f;
        }
        limb->velocity.x *= 0.8f;
        limb->velocity.z *= 0.8f;
    }
    
    limb->velocity = vec3_scale(limb->velocity, damping);
    limb->angular_velocity = vec3_scale(limb->angular_velocity, angular_damping);
}

void humanoid_update_ragdoll(HumanoidModel* model, float floor_y, float dt) {
    if (!model->ragdoll.ragdoll_active) return;
    
    model->ragdoll.ragdoll_time += dt;
    
    float gravity = -28.0f;
    float bounce = 0.22f;
    float friction = 0.68f;
    float angular_damping = 0.90f;
    float linear_damping = 0.96f;
    
    model->ragdoll.velocity.y += gravity * dt;
    model->ragdoll.position = vec3_add(model->ragdoll.position, vec3_scale(model->ragdoll.velocity, dt));
    model->ragdoll.rotation = vec3_add(model->ragdoll.rotation, vec3_scale(model->ragdoll.angular_velocity, dt));
    
    float ground_offset = HUMANOID_VOXEL_SIZE * 0.5f;
    
    if (model->ragdoll.position.y < floor_y + ground_offset) {
        model->ragdoll.position.y = floor_y + ground_offset;
        
        if (model->ragdoll.velocity.y < -0.5f) {
            model->ragdoll.velocity.y = -model->ragdoll.velocity.y * bounce;
            
            model->ragdoll.angular_velocity.x += model->ragdoll.velocity.z * 2.0f;
            model->ragdoll.angular_velocity.z -= model->ragdoll.velocity.x * 2.0f;
        } else {
            model->ragdoll.velocity.y = 0.0f;
        }
        
        model->ragdoll.velocity.x *= friction;
        model->ragdoll.velocity.z *= friction;
        model->ragdoll.angular_velocity = vec3_scale(model->ragdoll.angular_velocity, friction);
    }
    
    Vec3 torso_world = model->ragdoll.position;
    torso_world.y += model->dims.leg_length;
    
    Vec3 head_anchor = torso_world;
    head_anchor.y += model->dims.body_height;
    update_ragdoll_limb(&model->ragdoll.head, head_anchor, model->dims.head_size * 0.5f, floor_y, dt);
    
    Vec3 shoulder_left = torso_world;
    shoulder_left.x -= model->dims.body_width * 0.5f;
    shoulder_left.y += model->dims.body_height * 0.8f;
    update_ragdoll_limb(&model->ragdoll.arm_left, shoulder_left, model->dims.arm_length * 0.8f, floor_y, dt);
    
    Vec3 shoulder_right = torso_world;
    shoulder_right.x += model->dims.body_width * 0.5f;
    shoulder_right.y += model->dims.body_height * 0.8f;
    update_ragdoll_limb(&model->ragdoll.arm_right, shoulder_right, model->dims.arm_length * 0.8f, floor_y, dt);
    
    Vec3 hip_left = torso_world;
    hip_left.x -= model->dims.body_width * 0.3f;
    update_ragdoll_limb(&model->ragdoll.leg_left, hip_left, model->dims.leg_length * 0.8f, floor_y, dt);
    
    Vec3 hip_right = torso_world;
    hip_right.x += model->dims.body_width * 0.3f;
    update_ragdoll_limb(&model->ragdoll.leg_right, hip_right, model->dims.leg_length * 0.8f, floor_y, dt);
    
    model->ragdoll.angular_velocity = vec3_scale(model->ragdoll.angular_velocity, angular_damping);
    model->ragdoll.velocity.x *= linear_damping;
    model->ragdoll.velocity.z *= linear_damping;
}

static bool has_active_voxel_in_part(const HumanoidModel* model, HumanoidPart part) {
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (model->voxels[i].active && model->voxels[i].part == part) {
            return true;
        }
    }
    return false;
}

static bool parts_adjacent(const HumanoidModel* model, HumanoidPart part_a, HumanoidPart part_b,
                           Vec3 base_pos, const HumanoidPose* pose, float threshold) {
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active || model->voxels[i].part != part_a) continue;
        Vec3 pos_a = humanoid_transform_voxel(&model->voxels[i], base_pos, &model->dims, pose);
        
        for (int32_t j = 0; j < model->voxel_count; j++) {
            if (!model->voxels[j].active || model->voxels[j].part != part_b) continue;
            Vec3 pos_b = humanoid_transform_voxel(&model->voxels[j], base_pos, &model->dims, pose);
            
            float dist = vec3_length(vec3_sub(pos_a, pos_b));
            if (dist < threshold) {
                return true;
            }
        }
    }
    return false;
}

int32_t humanoid_check_connectivity(HumanoidModel* model, Vec3 base_pos, const HumanoidPose* pose,
                                     Vec3 base_color, Vec3* out_positions, Vec3* out_colors, int32_t max_out) {
    int32_t dropped_count = 0;
    float connect_threshold = HUMANOID_VOXEL_SIZE * 3.0f;
    
    bool body_exists = has_active_voxel_in_part(model, HUMANOID_PART_BODY);
    
    if (!body_exists) {
        for (int32_t i = 0; i < model->voxel_count && dropped_count < max_out; i++) {
            if (!model->voxels[i].active) continue;
            model->voxels[i].active = false;
            model->current_mass -= model->voxels[i].mass;
            out_positions[dropped_count] = humanoid_transform_voxel(&model->voxels[i], base_pos, &model->dims, pose);
            out_colors[dropped_count] = get_part_color(model->voxels[i].part, base_color);
            dropped_count++;
        }
        return dropped_count;
    }
    
    bool head_connected = parts_adjacent(model, HUMANOID_PART_HEAD, HUMANOID_PART_BODY, base_pos, pose, connect_threshold);
    bool left_arm_connected = parts_adjacent(model, HUMANOID_PART_ARM_LEFT, HUMANOID_PART_BODY, base_pos, pose, connect_threshold);
    bool right_arm_connected = parts_adjacent(model, HUMANOID_PART_ARM_RIGHT, HUMANOID_PART_BODY, base_pos, pose, connect_threshold);
    bool left_leg_connected = parts_adjacent(model, HUMANOID_PART_LEG_LEFT, HUMANOID_PART_BODY, base_pos, pose, connect_threshold);
    bool right_leg_connected = parts_adjacent(model, HUMANOID_PART_LEG_RIGHT, HUMANOID_PART_BODY, base_pos, pose, connect_threshold);
    
    for (int32_t i = 0; i < model->voxel_count && dropped_count < max_out; i++) {
        if (!model->voxels[i].active) continue;
        
        bool should_drop = false;
        switch (model->voxels[i].part) {
            case HUMANOID_PART_HEAD:
                should_drop = !head_connected;
                break;
            case HUMANOID_PART_ARM_LEFT:
                should_drop = !left_arm_connected;
                break;
            case HUMANOID_PART_ARM_RIGHT:
                should_drop = !right_arm_connected;
                break;
            case HUMANOID_PART_LEG_LEFT:
                should_drop = !left_leg_connected;
                break;
            case HUMANOID_PART_LEG_RIGHT:
                should_drop = !right_leg_connected;
                break;
            default:
                break;
        }
        
        if (should_drop) {
            model->voxels[i].active = false;
            model->current_mass -= model->voxels[i].mass;
            out_positions[dropped_count] = humanoid_transform_voxel(&model->voxels[i], base_pos, &model->dims, pose);
            out_colors[dropped_count] = get_part_color(model->voxels[i].part, base_color);
            dropped_count++;
        }
    }
    
    return dropped_count;
}

bool humanoid_head_connected(const HumanoidModel* model) {
    bool has_head = has_active_voxel_in_part(model, HUMANOID_PART_HEAD);
    bool has_body = has_active_voxel_in_part(model, HUMANOID_PART_BODY);
    
    if (!has_head || !has_body) return false;
    
    HumanoidPose neutral = {0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 base = vec3_zero();
    float threshold = HUMANOID_VOXEL_SIZE * 3.0f;
    
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active || model->voxels[i].part != HUMANOID_PART_HEAD) continue;
        Vec3 head_pos = humanoid_transform_voxel(&model->voxels[i], base, &model->dims, &neutral);
        
        for (int32_t j = 0; j < model->voxel_count; j++) {
            if (!model->voxels[j].active || model->voxels[j].part != HUMANOID_PART_BODY) continue;
            Vec3 body_pos = humanoid_transform_voxel(&model->voxels[j], base, &model->dims, &neutral);
            
            if (vec3_length(vec3_sub(head_pos, body_pos)) < threshold) {
                return true;
            }
        }
    }
    return false;
}

bool humanoid_should_die(const HumanoidModel* model) {
    if (!humanoid_head_connected(model)) return true;
    float mass_ratio = humanoid_get_mass_ratio(model);
    if (mass_ratio < 0.62f) return true;
    int32_t active_voxels = 0;
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (model->voxels[i].active) active_voxels++;
    }
    if (model->voxel_count > 0) {
        float fill = (float)active_voxels / (float)model->voxel_count;
        if (fill < 0.5f) return true;
    }
    bool legs_gone = !has_active_voxel_in_part(model, HUMANOID_PART_LEG_LEFT) && !has_active_voxel_in_part(model, HUMANOID_PART_LEG_RIGHT);
    bool arms_gone = !has_active_voxel_in_part(model, HUMANOID_PART_ARM_LEFT) && !has_active_voxel_in_part(model, HUMANOID_PART_ARM_RIGHT);
    if (legs_gone || arms_gone) return true;
    if (!has_active_voxel_in_part(model, HUMANOID_PART_BODY)) return true;
    return false;
}

static bool is_part_connected_to_body(const HumanoidModel* model, HumanoidPart part) {
    if (part == HUMANOID_PART_BODY) return true;
    
    HumanoidPose neutral = {0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 base = vec3_zero();
    float threshold = HUMANOID_VOXEL_SIZE * 3.0f;
    
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active || model->voxels[i].part != part) continue;
        Vec3 part_pos = humanoid_transform_voxel(&model->voxels[i], base, &model->dims, &neutral);
        
        for (int32_t j = 0; j < model->voxel_count; j++) {
            if (!model->voxels[j].active || model->voxels[j].part != HUMANOID_PART_BODY) continue;
            Vec3 body_pos = humanoid_transform_voxel(&model->voxels[j], base, &model->dims, &neutral);
            
            if (vec3_length(vec3_sub(part_pos, body_pos)) < threshold) {
                return true;
            }
        }
    }
    return false;
}

static bool can_heal_voxel_at_index(const HumanoidModel* model, int32_t index) {
    if (model->voxels[index].active) return false;
    
    HumanoidPart part = model->voxels[index].part;
    HumanoidPose neutral = {0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 base = vec3_zero();
    float threshold = HUMANOID_VOXEL_SIZE * 2.5f;
    
    Vec3 target_pos = humanoid_transform_voxel(&model->voxels[index], base, &model->dims, &neutral);
    
    for (int32_t i = 0; i < model->voxel_count; i++) {
        if (!model->voxels[i].active) continue;
        
        HumanoidPart other_part = model->voxels[i].part;
        bool can_connect = (other_part == part) || 
                          (part != HUMANOID_PART_BODY && other_part == HUMANOID_PART_BODY) ||
                          (part == HUMANOID_PART_HEAD && other_part == HUMANOID_PART_BODY);
        
        if (!can_connect) continue;
        
        Vec3 other_pos = humanoid_transform_voxel(&model->voxels[i], base, &model->dims, &neutral);
        float dist = vec3_length(vec3_sub(target_pos, other_pos));
        
        if (dist < threshold) {
            if (other_part == part) return true;
            if (other_part == HUMANOID_PART_BODY) return true;
        }
    }
    
    return false;
}

bool humanoid_heal_voxel(HumanoidModel* model, Vec3 color) {
    static const HumanoidPart heal_priority[] = {
        HUMANOID_PART_BODY,
        HUMANOID_PART_HEAD,
        HUMANOID_PART_ARM_LEFT,
        HUMANOID_PART_ARM_RIGHT,
        HUMANOID_PART_LEG_LEFT,
        HUMANOID_PART_LEG_RIGHT
    };
    static const int32_t priority_count = 6;
    
    for (int32_t p = 0; p < priority_count; p++) {
        HumanoidPart target_part = heal_priority[p];
        
        if (target_part != HUMANOID_PART_BODY && !is_part_connected_to_body(model, target_part)) {
            bool has_body = false;
            for (int32_t i = 0; i < model->voxel_count; i++) {
                if (model->voxels[i].active && model->voxels[i].part == HUMANOID_PART_BODY) {
                    has_body = true;
                    break;
                }
            }
            if (!has_body) continue;
        }
        
        for (int32_t i = 0; i < model->voxel_count; i++) {
            if (model->voxels[i].part != target_part) continue;
            if (!can_heal_voxel_at_index(model, i)) continue;
            
            model->voxels[i].active = true;
            model->voxels[i].mass = 1.0f;
            model->voxels[i].has_color_override = true;
            model->voxels[i].color_override = color;
            model->current_mass += model->voxels[i].mass;
            return true;
        }
    }
    
    return false;
}

Vec3 humanoid_transform_voxel_ragdoll(const HumanoidVoxel* voxel, const HumanoidModel* model) {
    if (!model->ragdoll.ragdoll_active) {
        HumanoidPose neutral = {0.0f, 0.0f, 0.0f, 0.0f};
        return humanoid_transform_voxel(voxel, model->ragdoll.position, &model->dims, &neutral);
    }
    
    const RagdollLimb* limb = NULL;
    Vec3 limb_anchor = vec3_zero();
    
    Vec3 torso_base = model->ragdoll.position;
    torso_base.y += model->dims.leg_length;
    
    switch (voxel->part) {
        case HUMANOID_PART_HEAD:
            limb = &model->ragdoll.head;
            limb_anchor = torso_base;
            limb_anchor.y += model->dims.body_height;
            break;
        case HUMANOID_PART_BODY:
            limb = &model->ragdoll.torso;
            limb_anchor = torso_base;
            break;
        case HUMANOID_PART_ARM_LEFT:
            limb = &model->ragdoll.arm_left;
            limb_anchor = torso_base;
            limb_anchor.x -= model->dims.body_width * 0.5f;
            limb_anchor.y += model->dims.body_height * 0.8f;
            break;
        case HUMANOID_PART_ARM_RIGHT:
            limb = &model->ragdoll.arm_right;
            limb_anchor = torso_base;
            limb_anchor.x += model->dims.body_width * 0.5f;
            limb_anchor.y += model->dims.body_height * 0.8f;
            break;
        case HUMANOID_PART_LEG_LEFT:
            limb = &model->ragdoll.leg_left;
            limb_anchor = torso_base;
            limb_anchor.x -= model->dims.body_width * 0.3f;
            break;
        case HUMANOID_PART_LEG_RIGHT:
            limb = &model->ragdoll.leg_right;
            limb_anchor = torso_base;
            limb_anchor.x += model->dims.body_width * 0.3f;
            break;
    }
    
    if (!limb) return model->ragdoll.position;
    
    float sin_rx = sinf(limb->rotation.x + model->ragdoll.rotation.x);
    float cos_rx = cosf(limb->rotation.x + model->ragdoll.rotation.x);
    float sin_ry = sinf(limb->rotation.y + model->ragdoll.rotation.y);
    float cos_ry = cosf(limb->rotation.y + model->ragdoll.rotation.y);
    float sin_rz = sinf(limb->rotation.z + model->ragdoll.rotation.z);
    float cos_rz = cosf(limb->rotation.z + model->ragdoll.rotation.z);
    
    Vec3 local = voxel->local_offset;
    
    Vec3 rotated;
    float temp_y = local.y * cos_rx - local.z * sin_rx;
    float temp_z = local.y * sin_rx + local.z * cos_rx;
    local.y = temp_y;
    local.z = temp_z;
    
    float temp_x = local.x * cos_ry + local.z * sin_ry;
    temp_z = -local.x * sin_ry + local.z * cos_ry;
    local.x = temp_x;
    local.z = temp_z;
    
    temp_x = local.x * cos_rz - local.y * sin_rz;
    temp_y = local.x * sin_rz + local.y * cos_rz;
    rotated.x = temp_x;
    rotated.y = temp_y;
    rotated.z = local.z;
    
    Vec3 result = vec3_add(limb_anchor, vec3_add(limb->position, rotated));
    return result;
}
