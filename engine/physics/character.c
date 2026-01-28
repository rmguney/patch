#include "character.h"
#include "rigidbody.h"
#include "engine/voxel/bvh.h"
#include <math.h>

void character_init(Character *character, Vec3 start_position)
{
    if (!character)
        return;

    character->position = start_position;
    character->velocity = vec3_zero();
    character->radius = CHAR_CAPSULE_RADIUS;
    character->height = CHAR_CAPSULE_HEIGHT;
    character->step_height = CHAR_STEP_HEIGHT;
    character->is_grounded = false;
    character->is_sliding = false;
    character->ground_normal = vec3_create(0.0f, 1.0f, 0.0f);
}

static void get_capsule_sample_points(Character *character, Vec3 points[CHAR_SAMPLE_POINTS])
{
    Vec3 pos = character->position;
    float r = character->radius;
    float h = character->height;
    float bottom_y = pos.y;
    float top_y = pos.y + h;
    float middle_y = pos.y + h * 0.5f;

    float angle_step = K_PI * 0.5f;
    for (int32_t i = 0; i < 4; i++)
    {
        float angle = (float)i * angle_step;
        float dx = cosf(angle) * r;
        float dz = sinf(angle) * r;

        points[i] = vec3_create(pos.x + dx, bottom_y, pos.z + dz);
        points[i + 4] = vec3_create(pos.x + dx, top_y, pos.z + dz);
        points[i + 8] = vec3_create(pos.x + dx, middle_y, pos.z + dz);
    }
}

static bool check_terrain_collision(VoxelVolume *terrain, Vec3 point, Vec3 *out_normal)
{
    if (!terrain)
        return false;

    uint8_t mat = volume_get_at(terrain, point);
    if (mat == 0)
        return false;

    float probe_dist = terrain->voxel_size * 0.5f;
    float dx = (volume_get_at(terrain, vec3_create(point.x + probe_dist, point.y, point.z)) != 0 ? 1.0f : 0.0f) -
               (volume_get_at(terrain, vec3_create(point.x - probe_dist, point.y, point.z)) != 0 ? 1.0f : 0.0f);
    float dy = (volume_get_at(terrain, vec3_create(point.x, point.y + probe_dist, point.z)) != 0 ? 1.0f : 0.0f) -
               (volume_get_at(terrain, vec3_create(point.x, point.y - probe_dist, point.z)) != 0 ? 1.0f : 0.0f);
    float dz = (volume_get_at(terrain, vec3_create(point.x, point.y, point.z + probe_dist)) != 0 ? 1.0f : 0.0f) -
               (volume_get_at(terrain, vec3_create(point.x, point.y, point.z - probe_dist)) != 0 ? 1.0f : 0.0f);

    Vec3 gradient = vec3_create(-dx, -dy, -dz);
    float len = vec3_length(gradient);
    if (len > K_EPSILON)
        *out_normal = vec3_scale(gradient, 1.0f / len);
    else
        *out_normal = vec3_create(0.0f, 1.0f, 0.0f);

    return true;
}

static bool check_object_collision(VoxelObjectWorld *objects, Vec3 point, Vec3 *out_normal)
{
    if (!objects || !objects->bvh || objects->bvh->node_count <= 0)
        return false;

    BVHQueryResult candidates = bvh_query_sphere(objects->bvh, point, CHAR_CAPSULE_RADIUS);

    for (int32_t c = 0; c < candidates.count; c++)
    {
        int32_t i = candidates.indices[c];
        if (i < 0 || i >= objects->object_count)
            continue;

        VoxelObject *obj = &objects->objects[i];
        if (!obj->active)
            continue;

        Vec3 delta = vec3_sub(point, obj->position);
        float dist = vec3_length(delta);
        if (dist > obj->radius)
            continue;

        Vec3 local = quat_rotate_vec3(quat_conjugate(obj->orientation), delta);
        Vec3 he = obj->shape_half_extents;

        if (fabsf(local.x) <= he.x &&
            fabsf(local.y) <= he.y &&
            fabsf(local.z) <= he.z)
        {
            float dx = he.x - fabsf(local.x);
            float dy = he.y - fabsf(local.y);
            float dz = he.z - fabsf(local.z);

            float mat3[9];
            quat_to_mat3(obj->orientation, mat3);

            if (dx <= dy && dx <= dz)
            {
                float sign = local.x >= 0.0f ? 1.0f : -1.0f;
                *out_normal = vec3_create(mat3[0] * sign, mat3[3] * sign, mat3[6] * sign);
            }
            else if (dy <= dx && dy <= dz)
            {
                float sign = local.y >= 0.0f ? 1.0f : -1.0f;
                *out_normal = vec3_create(mat3[1] * sign, mat3[4] * sign, mat3[7] * sign);
            }
            else
            {
                float sign = local.z >= 0.0f ? 1.0f : -1.0f;
                *out_normal = vec3_create(mat3[2] * sign, mat3[5] * sign, mat3[8] * sign);
            }
            return true;
        }
    }

    return false;
}

static bool check_collision(VoxelVolume *terrain, VoxelObjectWorld *objects, Vec3 point, Vec3 *out_normal)
{
    Vec3 terrain_normal = vec3_zero();
    Vec3 object_normal = vec3_zero();

    bool hit_terrain = check_terrain_collision(terrain, point, &terrain_normal);
    bool hit_object = check_object_collision(objects, point, &object_normal);

    if (hit_terrain && hit_object)
    {
        *out_normal = vec3_normalize(vec3_add(terrain_normal, object_normal));
        return true;
    }
    else if (hit_terrain)
    {
        *out_normal = terrain_normal;
        return true;
    }
    else if (hit_object)
    {
        *out_normal = object_normal;
        return true;
    }

    return false;
}

static bool check_capsule_collision(Character *character,
                                     VoxelVolume *terrain,
                                     VoxelObjectWorld *objects,
                                     Vec3 *out_normal,
                                     Vec3 *out_contact)
{
    Vec3 sample_points[CHAR_SAMPLE_POINTS];
    get_capsule_sample_points(character, sample_points);

    Vec3 total_normal = vec3_zero();
    int32_t collision_count = 0;
    Vec3 avg_contact = vec3_zero();

    for (int32_t i = 0; i < CHAR_SAMPLE_POINTS; i++)
    {
        Vec3 normal;
        if (check_collision(terrain, objects, sample_points[i], &normal))
        {
            total_normal = vec3_add(total_normal, normal);
            avg_contact = vec3_add(avg_contact, sample_points[i]);
            collision_count++;
        }
    }

    if (collision_count > 0)
    {
        *out_normal = vec3_normalize(total_normal);
        *out_contact = vec3_scale(avg_contact, 1.0f / (float)collision_count);
        return true;
    }

    return false;
}

static Vec3 project_velocity_onto_plane(Vec3 velocity, Vec3 normal)
{
    float v_dot_n = vec3_dot(velocity, normal);
    return vec3_sub(velocity, vec3_scale(normal, v_dot_n));
}

static bool try_step_up(Character *character,
                        VoxelVolume *terrain,
                        VoxelObjectWorld *objects,
                        Vec3 move_dir)
{
    Vec3 original_pos = character->position;

    character->position.y += character->step_height;

    Vec3 collision_normal, contact_point;
    if (check_capsule_collision(character, terrain, objects, &collision_normal, &contact_point))
    {
        character->position = original_pos;
        return false;
    }

    float move_dist = vec3_length(move_dir);
    if (move_dist > K_EPSILON)
    {
        Vec3 step_pos = vec3_add(character->position, move_dir);
        character->position = step_pos;

        if (check_capsule_collision(character, terrain, objects, &collision_normal, &contact_point))
        {
            character->position = original_pos;
            return false;
        }
    }

    Vec3 down_pos = character->position;
    down_pos.y -= character->step_height;

    Vec3 test_pos = character->position;
    float step_down = character->step_height / 4.0f;

    for (int32_t i = 0; i < 4; i++)
    {
        test_pos.y -= step_down;
        character->position = test_pos;

        if (check_capsule_collision(character, terrain, objects, &collision_normal, &contact_point))
        {
            character->position.y += step_down;
            return true;
        }
    }

    character->position = original_pos;
    return false;
}

void character_move(Character *character,
                    VoxelVolume *terrain,
                    VoxelObjectWorld *objects,
                    Vec3 move_input,
                    float dt)
{
    if (!character)
        return;

    character->velocity = vec3_add(character->velocity, vec3_scale(vec3_create(0.0f, PHYS_GRAVITY_Y, 0.0f), dt));
    character->velocity.x = move_input.x;
    character->velocity.z = move_input.z;

    Vec3 move_delta = vec3_scale(character->velocity, dt);

    for (int32_t iter = 0; iter < CHAR_SLIDE_ITERATIONS; iter++)
    {
        float move_len = vec3_length(move_delta);
        if (move_len < K_EPSILON)
            break;

        character->position = vec3_add(character->position, move_delta);

        Vec3 collision_normal, contact_point;
        if (check_capsule_collision(character, terrain, objects, &collision_normal, &contact_point))
        {
            character->position = vec3_sub(character->position, move_delta);

            if (collision_normal.y < 0.7f && vec3_length_sq(vec3_create(move_delta.x, 0.0f, move_delta.z)) > K_EPSILON * K_EPSILON)
            {
                Vec3 horizontal_move = vec3_create(move_delta.x, 0.0f, move_delta.z);
                if (try_step_up(character, terrain, objects, horizontal_move))
                {
                    move_delta = vec3_zero();
                    continue;
                }
            }

            move_delta = project_velocity_onto_plane(move_delta, collision_normal);
            character->velocity = project_velocity_onto_plane(character->velocity, collision_normal);

            if (collision_normal.y > 0.7f)
            {
                character->is_grounded = true;
                character->ground_normal = collision_normal;
            }
        }
        else
        {
            break;
        }
    }

    Vec3 ground_check_pos = character->position;
    ground_check_pos.y -= CHAR_GROUND_CHECK_DIST;

    Vec3 check_normal;
    Character temp_char = *character;
    temp_char.position = ground_check_pos;

    Vec3 contact;
    if (check_capsule_collision(&temp_char, terrain, objects, &check_normal, &contact))
    {
        if (check_normal.y > 0.7f)
        {
            character->is_grounded = true;
            character->ground_normal = check_normal;
        }
    }
    else
    {
        character->is_grounded = false;
    }

    character->velocity = vec3_clamp_length(character->velocity, PHYS_MAX_LINEAR_VELOCITY);
}

void character_jump(Character *character, float jump_velocity)
{
    if (!character || !character->is_grounded)
        return;

    character->velocity.y = jump_velocity;
    character->is_grounded = false;
}

bool character_is_grounded(Character *character)
{
    return character ? character->is_grounded : false;
}

Vec3 character_get_feet_position(Character *character)
{
    return character ? character->position : vec3_zero();
}

Vec3 character_get_head_position(Character *character)
{
    if (!character)
        return vec3_zero();
    return vec3_create(character->position.x,
                       character->position.y + character->height,
                       character->position.z);
}
