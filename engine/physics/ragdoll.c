#include "ragdoll.h"
#include <stdlib.h>
#include <string.h>

RagdollSystem *ragdoll_system_create(void)
{
    RagdollSystem *system = (RagdollSystem *)calloc(1, sizeof(RagdollSystem));
    if (!system)
        return NULL;

    system->ragdoll_count = 0;
    system->gravity = RAGDOLL_GRAVITY;
    system->damping = RAGDOLL_DAMPING;

    for (int32_t i = 0; i < RAGDOLL_MAX_RAGDOLLS; i++)
        system->ragdolls[i].active = false;

    return system;
}

void ragdoll_system_destroy(RagdollSystem *system)
{
    if (system)
        free(system);
}

static void init_ragdoll_part(RagdollPart *part, Vec3 position, Vec3 half_extents, float mass)
{
    part->position = position;
    part->prev_position = position;
    part->velocity = vec3_zero();
    part->half_extents = half_extents;
    part->mass = mass;
    part->inv_mass = mass > K_EPSILON ? 1.0f / mass : 0.0f;
}

static void init_ragdoll_constraint(RagdollConstraint *constraint, RagdollConstraintType type,
                                     int32_t part_a, int32_t part_b,
                                     Vec3 anchor_a, Vec3 anchor_b, float rest_length)
{
    constraint->type = type;
    constraint->part_a = part_a;
    constraint->part_b = part_b;
    constraint->anchor_a = anchor_a;
    constraint->anchor_b = anchor_b;
    constraint->rest_length = rest_length;
    constraint->min_angle = -K_PI * 0.5f;
    constraint->max_angle = K_PI * 0.5f;
}

int32_t ragdoll_spawn(RagdollSystem *system, Vec3 position, float scale)
{
    if (!system)
        return -1;

    int32_t slot = -1;
    for (int32_t i = 0; i < RAGDOLL_MAX_RAGDOLLS; i++)
    {
        if (!system->ragdolls[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        return -1;

    Ragdoll *ragdoll = &system->ragdolls[slot];
    memset(ragdoll, 0, sizeof(Ragdoll));

    float head_size = 0.15f * scale;
    float torso_height = 0.4f * scale;
    float torso_width = 0.25f * scale;
    float limb_length = 0.3f * scale;
    float limb_width = 0.08f * scale;

    Vec3 head_pos = vec3_add(position, vec3_create(0.0f, torso_height * 0.5f + head_size, 0.0f));
    Vec3 torso_pos = position;
    Vec3 left_arm_pos = vec3_add(position, vec3_create(-torso_width - limb_length * 0.5f, torso_height * 0.3f, 0.0f));
    Vec3 right_arm_pos = vec3_add(position, vec3_create(torso_width + limb_length * 0.5f, torso_height * 0.3f, 0.0f));
    Vec3 left_leg_pos = vec3_add(position, vec3_create(-torso_width * 0.3f, -torso_height * 0.5f - limb_length * 0.5f, 0.0f));
    Vec3 right_leg_pos = vec3_add(position, vec3_create(torso_width * 0.3f, -torso_height * 0.5f - limb_length * 0.5f, 0.0f));

    init_ragdoll_part(&ragdoll->parts[RAGDOLL_PART_HEAD], head_pos,
                      vec3_create(head_size, head_size, head_size), 4.0f);
    init_ragdoll_part(&ragdoll->parts[RAGDOLL_PART_TORSO], torso_pos,
                      vec3_create(torso_width, torso_height * 0.5f, torso_width * 0.5f), 30.0f);
    init_ragdoll_part(&ragdoll->parts[RAGDOLL_PART_LEFT_ARM], left_arm_pos,
                      vec3_create(limb_length * 0.5f, limb_width, limb_width), 3.0f);
    init_ragdoll_part(&ragdoll->parts[RAGDOLL_PART_RIGHT_ARM], right_arm_pos,
                      vec3_create(limb_length * 0.5f, limb_width, limb_width), 3.0f);
    init_ragdoll_part(&ragdoll->parts[RAGDOLL_PART_LEFT_LEG], left_leg_pos,
                      vec3_create(limb_width, limb_length * 0.5f, limb_width), 8.0f);
    init_ragdoll_part(&ragdoll->parts[RAGDOLL_PART_RIGHT_LEG], right_leg_pos,
                      vec3_create(limb_width, limb_length * 0.5f, limb_width), 8.0f);
    ragdoll->part_count = RAGDOLL_MAX_PARTS;

    init_ragdoll_constraint(&ragdoll->constraints[0], RAGDOLL_CONSTRAINT_BALL_SOCKET,
                            RAGDOLL_PART_HEAD, RAGDOLL_PART_TORSO,
                            vec3_create(0.0f, -head_size, 0.0f),
                            vec3_create(0.0f, torso_height * 0.5f, 0.0f),
                            head_size * 0.5f);

    init_ragdoll_constraint(&ragdoll->constraints[1], RAGDOLL_CONSTRAINT_BALL_SOCKET,
                            RAGDOLL_PART_LEFT_ARM, RAGDOLL_PART_TORSO,
                            vec3_create(limb_length * 0.5f, 0.0f, 0.0f),
                            vec3_create(-torso_width, torso_height * 0.3f, 0.0f),
                            limb_width);

    init_ragdoll_constraint(&ragdoll->constraints[2], RAGDOLL_CONSTRAINT_BALL_SOCKET,
                            RAGDOLL_PART_RIGHT_ARM, RAGDOLL_PART_TORSO,
                            vec3_create(-limb_length * 0.5f, 0.0f, 0.0f),
                            vec3_create(torso_width, torso_height * 0.3f, 0.0f),
                            limb_width);

    init_ragdoll_constraint(&ragdoll->constraints[3], RAGDOLL_CONSTRAINT_BALL_SOCKET,
                            RAGDOLL_PART_LEFT_LEG, RAGDOLL_PART_TORSO,
                            vec3_create(0.0f, limb_length * 0.5f, 0.0f),
                            vec3_create(-torso_width * 0.3f, -torso_height * 0.5f, 0.0f),
                            limb_width);

    init_ragdoll_constraint(&ragdoll->constraints[4], RAGDOLL_CONSTRAINT_BALL_SOCKET,
                            RAGDOLL_PART_RIGHT_LEG, RAGDOLL_PART_TORSO,
                            vec3_create(0.0f, limb_length * 0.5f, 0.0f),
                            vec3_create(torso_width * 0.3f, -torso_height * 0.5f, 0.0f),
                            limb_width);

    ragdoll->constraint_count = 5;
    ragdoll->active = true;
    system->ragdoll_count++;

    return slot;
}

void ragdoll_despawn(RagdollSystem *system, int32_t ragdoll_index)
{
    if (!system || ragdoll_index < 0 || ragdoll_index >= RAGDOLL_MAX_RAGDOLLS)
        return;

    Ragdoll *ragdoll = &system->ragdolls[ragdoll_index];
    if (!ragdoll->active)
        return;

    ragdoll->active = false;
    system->ragdoll_count--;
}

static void integrate_verlet(RagdollPart *part, float gravity, float damping, float dt)
{
    Vec3 temp = part->position;

    Vec3 velocity = vec3_sub(part->position, part->prev_position);
    velocity = vec3_scale(velocity, damping);

    Vec3 gravity_accel = vec3_create(0.0f, gravity * dt * dt, 0.0f);

    part->position = vec3_add(part->position, vec3_add(velocity, gravity_accel));
    part->prev_position = temp;

    part->velocity = vec3_scale(vec3_sub(part->position, part->prev_position), 1.0f / dt);
}

static void solve_distance_constraint(RagdollPart *part_a, RagdollPart *part_b,
                                       Vec3 anchor_a, Vec3 anchor_b, float rest_length)
{
    Vec3 world_anchor_a = vec3_add(part_a->position, anchor_a);
    Vec3 world_anchor_b = vec3_add(part_b->position, anchor_b);

    Vec3 delta = vec3_sub(world_anchor_b, world_anchor_a);
    float dist = vec3_length(delta);

    if (dist < K_EPSILON)
        return;

    float diff = (dist - rest_length) / dist;
    Vec3 correction = vec3_scale(delta, diff);

    float total_inv_mass = part_a->inv_mass + part_b->inv_mass;
    if (total_inv_mass < K_EPSILON)
        return;

    float ratio_a = part_a->inv_mass / total_inv_mass;
    float ratio_b = part_b->inv_mass / total_inv_mass;

    part_a->position = vec3_add(part_a->position, vec3_scale(correction, ratio_a));
    part_b->position = vec3_sub(part_b->position, vec3_scale(correction, ratio_b));
}

static void solve_terrain_collision_part(RagdollPart *part, VoxelVolume *terrain)
{
    if (!terrain)
        return;

    Vec3 min_pt = vec3_sub(part->position, part->half_extents);
    Vec3 max_pt = vec3_add(part->position, part->half_extents);

    Vec3 sample_points[8];
    sample_points[0] = vec3_create(min_pt.x, min_pt.y, min_pt.z);
    sample_points[1] = vec3_create(max_pt.x, min_pt.y, min_pt.z);
    sample_points[2] = vec3_create(min_pt.x, max_pt.y, min_pt.z);
    sample_points[3] = vec3_create(max_pt.x, max_pt.y, min_pt.z);
    sample_points[4] = vec3_create(min_pt.x, min_pt.y, max_pt.z);
    sample_points[5] = vec3_create(max_pt.x, min_pt.y, max_pt.z);
    sample_points[6] = vec3_create(min_pt.x, max_pt.y, max_pt.z);
    sample_points[7] = vec3_create(max_pt.x, max_pt.y, max_pt.z);

    for (int32_t i = 0; i < 8; i++)
    {
        uint8_t mat = volume_get_at(terrain, sample_points[i]);
        if (mat == 0)
            continue;

        Vec3 local = vec3_sub(sample_points[i], part->position);

        Vec3 normal = vec3_zero();
        float push_dist = 0.0f;

        if (fabsf(local.x) >= fabsf(local.y) && fabsf(local.x) >= fabsf(local.z))
        {
            normal.x = local.x >= 0.0f ? 1.0f : -1.0f;
            push_dist = part->half_extents.x - fabsf(local.x) + terrain->voxel_size;
        }
        else if (fabsf(local.y) >= fabsf(local.z))
        {
            normal.y = local.y >= 0.0f ? 1.0f : -1.0f;
            push_dist = part->half_extents.y - fabsf(local.y) + terrain->voxel_size;
        }
        else
        {
            normal.z = local.z >= 0.0f ? 1.0f : -1.0f;
            push_dist = part->half_extents.z - fabsf(local.z) + terrain->voxel_size;
        }

        part->position = vec3_add(part->position, vec3_scale(normal, push_dist));
    }
}

void ragdoll_system_update(RagdollSystem *system, VoxelVolume *terrain, float dt)
{
    if (!system || dt <= 0.0f)
        return;

    for (int32_t r = 0; r < RAGDOLL_MAX_RAGDOLLS; r++)
    {
        Ragdoll *ragdoll = &system->ragdolls[r];
        if (!ragdoll->active)
            continue;

        for (int32_t p = 0; p < ragdoll->part_count; p++)
            integrate_verlet(&ragdoll->parts[p], system->gravity, system->damping, dt);

        for (int32_t iter = 0; iter < 4; iter++)
        {
            for (int32_t c = 0; c < ragdoll->constraint_count; c++)
            {
                RagdollConstraint *constraint = &ragdoll->constraints[c];
                RagdollPart *part_a = &ragdoll->parts[constraint->part_a];
                RagdollPart *part_b = &ragdoll->parts[constraint->part_b];

                solve_distance_constraint(part_a, part_b,
                                          constraint->anchor_a, constraint->anchor_b,
                                          constraint->rest_length);
            }

            if (terrain)
            {
                for (int32_t p = 0; p < ragdoll->part_count; p++)
                    solve_terrain_collision_part(&ragdoll->parts[p], terrain);
            }
        }
    }
}

Ragdoll *ragdoll_get(RagdollSystem *system, int32_t ragdoll_index)
{
    if (!system || ragdoll_index < 0 || ragdoll_index >= RAGDOLL_MAX_RAGDOLLS)
        return NULL;

    Ragdoll *ragdoll = &system->ragdolls[ragdoll_index];
    return ragdoll->active ? ragdoll : NULL;
}

int32_t ragdoll_system_active_count(RagdollSystem *system)
{
    return system ? system->ragdoll_count : 0;
}

void ragdoll_apply_impulse(RagdollSystem *system, int32_t ragdoll_index,
                           int32_t part_index, Vec3 impulse)
{
    Ragdoll *ragdoll = ragdoll_get(system, ragdoll_index);
    if (!ragdoll || part_index < 0 || part_index >= ragdoll->part_count)
        return;

    RagdollPart *part = &ragdoll->parts[part_index];
    if (part->inv_mass > 0.0f)
    {
        Vec3 delta = vec3_scale(impulse, part->inv_mass);
        part->position = vec3_add(part->position, delta);
    }
}
