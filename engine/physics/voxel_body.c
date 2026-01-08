#include "engine/physics/voxel_body.h"
#include "engine/physics/rigid_body.h"
#include "engine/core/profile.h"
#include <math.h>

/* Physics stability constants (Box2D-inspired) */
#define VOBJ_SLEEP_VELOCITY_THRESHOLD 0.08f   /* Linear velocity to start sleep timer */
#define VOBJ_SLEEP_ANGULAR_THRESHOLD 0.15f    /* Angular velocity to start sleep timer */
#define VOBJ_SLEEP_TIME_REQUIRED 0.3f         /* Time at low velocity before sleeping */
#define VOBJ_WAKE_VELOCITY_THRESHOLD 0.2f     /* Wake up if velocity exceeds this */
#define VOBJ_MIN_BOUNCE_VELOCITY 0.3f         /* Below this, drastically reduce bounce */
#define VOBJ_SETTLING_VELOCITY 0.4f           /* Below this, skip topple forces */
#define COLLISION_GROUND_ITERATIONS 3         /* Post-collision ground enforcement passes */

static void update_cached_bounds(VoxelObject *obj)
{
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    Vec3 pivot = vec3_add(obj->position, obj->center_of_mass_offset);

    float rot_mat[9];
    quat_to_mat3(obj->orientation, rot_mat);

    float lowest_y = 1e10f;
    float highest_y = -1e10f;
    float leftmost_x = 1e10f;
    float rightmost_x = -1e10f;
    float nearest_z = 1e10f;
    float farthest_z = -1e10f;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                if (obj->voxels[vobj_index(x, y, z)].material == 0)
                    continue;

                Vec3 local;
                local.x = ((float)x + 0.5f) * obj->voxel_size - half_size - obj->center_of_mass_offset.x;
                local.y = ((float)y + 0.5f) * obj->voxel_size - half_size - obj->center_of_mass_offset.y;
                local.z = ((float)z + 0.5f) * obj->voxel_size - half_size - obj->center_of_mass_offset.z;

                Vec3 rotated = mat3_transform_vec3(rot_mat, local);
                Vec3 world = vec3_add(pivot, rotated);
                float vhalf = obj->voxel_size * 0.5f;

                if (world.y - vhalf < lowest_y) lowest_y = world.y - vhalf;
                if (world.y + vhalf > highest_y) highest_y = world.y + vhalf;
                if (world.x - vhalf < leftmost_x) leftmost_x = world.x - vhalf;
                if (world.x + vhalf > rightmost_x) rightmost_x = world.x + vhalf;
                if (world.z - vhalf < nearest_z) nearest_z = world.z - vhalf;
                if (world.z + vhalf > farthest_z) farthest_z = world.z + vhalf;
            }
        }
    }

    obj->cached_lowest_y = lowest_y;
    obj->cached_highest_y = highest_y;
    obj->cached_bounds_x[0] = leftmost_x;
    obj->cached_bounds_x[1] = rightmost_x;
    obj->cached_bounds_z[0] = nearest_z;
    obj->cached_bounds_z[1] = farthest_z;
    obj->cached_rotation = obj->rotation;
    obj->bounds_dirty = false;
}

static void ensure_cached_bounds(VoxelObject *obj)
{
    if (!obj->bounds_dirty)
        return;
    update_cached_bounds(obj);
}

static void apply_topple_torque(VoxelObject *obj, const Bounds3D *bounds, float dt)
{
    /* Skip topple torque when object is nearly settled - prevents erratic rotations */
    float speed = vec3_length(obj->velocity);
    if (speed < VOBJ_SETTLING_VELOCITY)
        return;

    ensure_cached_bounds(obj);

    float floor_dist = obj->cached_lowest_y - bounds->min_y;
    if (floor_dist > 0.05f)
        return;

    Vec3 pivot = vec3_add(obj->position, obj->center_of_mass_offset);
    Vec3 lowest_point = vec3_create(obj->position.x, obj->cached_lowest_y, obj->position.z);
    Vec3 contact_to_com = vec3_sub(pivot, lowest_point);

    float torque_strength = 25.0f;
    obj->angular_velocity.z -= contact_to_com.x * torque_strength * dt;
    obj->angular_velocity.x += contact_to_com.z * torque_strength * dt;
}

static void resolve_rotated_ground_collision(VoxelObject *obj, const Bounds3D *bounds,
                                              float restitution, float friction)
{
    ensure_cached_bounds(obj);

    float lowest_y = obj->cached_lowest_y;
    float highest_y = obj->cached_highest_y;
    float leftmost_x = obj->cached_bounds_x[0];
    float rightmost_x = obj->cached_bounds_x[1];
    float nearest_z = obj->cached_bounds_z[0];
    float farthest_z = obj->cached_bounds_z[1];

    obj->on_ground = false;
    float ground_tolerance = 0.1f;

    /* Floor collision */
    if (lowest_y < bounds->min_y)
    {
        float penetration = bounds->min_y - lowest_y;
        obj->position.y += penetration;
        obj->on_ground = true;

        /* Velocity-dependent restitution: less bounce at low speeds */
        float impact_speed = fabsf(obj->velocity.y);
        float effective_restitution = (impact_speed > VOBJ_MIN_BOUNCE_VELOCITY)
            ? restitution : restitution * 0.1f;

        obj->velocity.y = -obj->velocity.y * effective_restitution;

        /* Kill small bounces completely */
        if (fabsf(obj->velocity.y) < 0.25f)
            obj->velocity.y = 0.0f;

        /* Angular response from impact - only for significant collisions */
        float speed = vec3_length(obj->velocity);
        if (speed > VOBJ_SETTLING_VELOCITY * 1.5f)
        {
            obj->angular_velocity.x += obj->velocity.z * 0.1f;
            obj->angular_velocity.z -= obj->velocity.x * 0.1f;
        }

        /* Floor friction - decelerate horizontal movement */
        obj->velocity.x *= friction;
        obj->velocity.z *= friction;

        /* Aggressive angular damping when on floor */
        float ang_friction = (speed < VOBJ_SETTLING_VELOCITY) ? friction * 0.5f : friction * 0.8f;
        obj->angular_velocity = vec3_scale(obj->angular_velocity, ang_friction);

        /* Kill small angular velocities when settling */
        if (speed < VOBJ_SETTLING_VELOCITY)
        {
            float ang_speed = vec3_length(obj->angular_velocity);
            if (ang_speed < 0.3f)
            {
                obj->angular_velocity = vec3_zero();
            }
        }
        obj->bounds_dirty = true;
    }
    else if (lowest_y < bounds->min_y + ground_tolerance && obj->velocity.y < 0.5f)
    {
        obj->on_ground = true;
    }

    (void)restitution;
    (void)highest_y;
    (void)leftmost_x;
    (void)rightmost_x;
    (void)nearest_z;
    (void)farthest_z;
}

/* Minimum relative velocity to apply collision impulse (prevents jitter) */
#define VOBJ_CONTACT_VELOCITY_THRESHOLD 0.15f

static void resolve_object_collision(VoxelObject *a, VoxelObject *b, float restitution)
{
    /* Sphere broadphase - fast rejection */
    Vec3 a_center = vec3_add(a->position, a->center_of_mass_offset);
    Vec3 b_center = vec3_add(b->position, b->center_of_mass_offset);
    Vec3 delta = vec3_sub(b_center, a_center);
    float dist = vec3_length(delta);
    float min_dist = a->radius + b->radius;

    if (dist >= min_dist || dist < 0.0001f)
        return;

    /* AABB narrowphase - use actual voxel bounds for better contact normal */
    ensure_cached_bounds(a);
    ensure_cached_bounds(b);

    float a_min_x = a->cached_bounds_x[0], a_max_x = a->cached_bounds_x[1];
    float a_min_y = a->cached_lowest_y,    a_max_y = a->cached_highest_y;
    float a_min_z = a->cached_bounds_z[0], a_max_z = a->cached_bounds_z[1];

    float b_min_x = b->cached_bounds_x[0], b_max_x = b->cached_bounds_x[1];
    float b_min_y = b->cached_lowest_y,    b_max_y = b->cached_highest_y;
    float b_min_z = b->cached_bounds_z[0], b_max_z = b->cached_bounds_z[1];

    float overlap_x = fminf(a_max_x, b_max_x) - fmaxf(a_min_x, b_min_x);
    float overlap_y = fminf(a_max_y, b_max_y) - fmaxf(a_min_y, b_min_y);
    float overlap_z = fminf(a_max_z, b_max_z) - fmaxf(a_min_z, b_min_z);

    /* AABB narrowphase rejection - skip if AABBs don't overlap */
    if (overlap_x <= 0.0f || overlap_y <= 0.0f || overlap_z <= 0.0f)
        return;

    /* Use sphere-based normal and overlap for proper 3D separation */
    Vec3 normal = vec3_scale(delta, 1.0f / dist);
    float overlap = min_dist - dist;

    float total_mass = a->mass + b->mass;
    if (total_mass < 0.001f) total_mass = 1.0f;
    float a_ratio = b->mass / total_mass;
    float b_ratio = a->mass / total_mass;

    /* Separate objects - slight over-correction to prevent persistent overlap */
    float separation = overlap * 1.02f;
    a->position = vec3_sub(a->position, vec3_scale(normal, separation * a_ratio));
    b->position = vec3_add(b->position, vec3_scale(normal, separation * b_ratio));

    /* Impulse response */
    Vec3 rel_vel = vec3_sub(a->velocity, b->velocity);
    float vel_along_normal = vec3_dot(rel_vel, normal);

    /* Objects separating - no impulse needed */
    if (vel_along_normal > 0.0f)
        return;

    float impact_speed = fabsf(vel_along_normal);

    /* Low relative velocity: just dampen normal velocity component (prevents jitter) */
    if (impact_speed < VOBJ_CONTACT_VELOCITY_THRESHOLD)
    {
        /* Remove velocity component pushing objects together */
        a->velocity = vec3_sub(a->velocity, vec3_scale(normal, vel_along_normal * a_ratio));
        b->velocity = vec3_add(b->velocity, vec3_scale(normal, vel_along_normal * b_ratio));
        return;
    }

    /* Wake sleeping objects only on significant collision */
    a->sleeping = false;
    b->sleeping = false;
    a->settle_timer = 0.0f;
    b->settle_timer = 0.0f;

    /* Velocity-dependent restitution for object-object collisions */
    float effective_restitution = (impact_speed > VOBJ_MIN_BOUNCE_VELOCITY)
        ? restitution : restitution * 0.3f;

    float j = -(1.0f + effective_restitution) * vel_along_normal;
    j /= (1.0f / a->mass + 1.0f / b->mass);
    Vec3 impulse = vec3_scale(normal, j);

    a->velocity = vec3_add(a->velocity, vec3_scale(impulse, 1.0f / a->mass));
    b->velocity = vec3_sub(b->velocity, vec3_scale(impulse, 1.0f / b->mass));

    /* Angular response from tangent with friction */
    Vec3 tangent_vel = vec3_sub(rel_vel, vec3_scale(normal, vel_along_normal));
    float tangent_speed = vec3_length(tangent_vel);
    if (tangent_speed > 0.01f)
    {
        Vec3 tangent = vec3_scale(tangent_vel, 1.0f / tangent_speed);
        float friction_coeff = 0.4f;
        float friction_j = fminf(tangent_speed * friction_coeff, fabsf(j) * friction_coeff);
        friction_j /= (1.0f / a->mass + 1.0f / b->mass);

        a->angular_velocity = vec3_add(a->angular_velocity,
                                        vec3_scale(vec3_cross(normal, tangent), friction_j / a->mass));
        b->angular_velocity = vec3_sub(b->angular_velocity,
                                        vec3_scale(vec3_cross(normal, tangent), friction_j / b->mass));
    }
}

/* Cleanup thresholds - only remove objects that fall out of bounds */
#define VOBJ_OUT_OF_BOUNDS_MARGIN 5.0f

static void cleanup_inactive_objects(VoxelObjectWorld *world)
{
    /* Compact the object array by removing inactive objects */
    int32_t write_idx = 0;
    for (int32_t read_idx = 0; read_idx < world->object_count; read_idx++)
    {
        if (world->objects[read_idx].active)
        {
            if (write_idx != read_idx)
            {
                world->objects[write_idx] = world->objects[read_idx];
            }
            write_idx++;
        }
    }
    world->object_count = write_idx;
}

static void clamp_velocity(Vec3 *vel, float max_speed)
{
    float speed_sq = vec3_length_sq(*vel);
    if (speed_sq > max_speed * max_speed)
    {
        float speed = sqrtf(speed_sq);
        *vel = vec3_scale(*vel, max_speed / speed);
    }
}

static void resolve_terrain_collision(VoxelObject *obj, VoxelVolume *terrain, float restitution, float friction)
{
    if (!terrain) return;

    Vec3 center = vec3_add(obj->position, obj->center_of_mass_offset);

    /* Sample points around the object's bounding sphere */
    const int32_t NUM_SAMPLES = 14;
    Vec3 sample_offsets[14] = {
        {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1},
        {-0.7f, -0.7f, 0}, {0.7f, -0.7f, 0}, {0, -0.7f, -0.7f}, {0, -0.7f, 0.7f},
        {-0.7f, 0, -0.7f}, {0.7f, 0, -0.7f}, {-0.7f, 0, 0.7f}, {0.7f, 0, 0.7f}
    };

    Vec3 total_push = vec3_zero();
    int32_t collision_count = 0;

    for (int32_t i = 0; i < NUM_SAMPLES; i++)
    {
        Vec3 sample_pos = vec3_add(center, vec3_scale(sample_offsets[i], obj->radius));

        if (volume_is_solid_at(terrain, sample_pos))
        {
            Vec3 push_dir = vec3_scale(sample_offsets[i], -1.0f);
            float push_len = vec3_length(push_dir);
            if (push_len > 0.001f)
            {
                push_dir = vec3_scale(push_dir, 1.0f / push_len);
            }
            else
            {
                push_dir = vec3_create(0, 1, 0);
            }

            total_push = vec3_add(total_push, push_dir);
            collision_count++;
        }
    }

    if (collision_count > 0)
    {
        Vec3 push_normal = vec3_normalize(total_push);
        float penetration = terrain->voxel_size * 0.5f;

        obj->position = vec3_add(obj->position, vec3_scale(push_normal, penetration));

        float vel_along_normal = vec3_dot(obj->velocity, push_normal);
        if (vel_along_normal < 0.0f)
        {
            float impact_speed = fabsf(vel_along_normal);
            float effective_restitution = (impact_speed > VOBJ_MIN_BOUNCE_VELOCITY)
                ? restitution : restitution * 0.1f;

            obj->velocity = vec3_sub(obj->velocity,
                vec3_scale(push_normal, vel_along_normal * (1.0f + effective_restitution)));

            if (fabsf(vec3_dot(obj->velocity, push_normal)) < 0.2f)
            {
                obj->velocity = vec3_sub(obj->velocity,
                    vec3_scale(push_normal, vec3_dot(obj->velocity, push_normal)));
            }

            Vec3 tangent_vel = vec3_sub(obj->velocity, vec3_scale(push_normal, vec3_dot(obj->velocity, push_normal)));
            obj->velocity = vec3_add(vec3_scale(push_normal, vec3_dot(obj->velocity, push_normal)),
                                      vec3_scale(tangent_vel, friction));

            obj->angular_velocity = vec3_scale(obj->angular_velocity, friction * 0.8f);
        }

        if (push_normal.y > 0.5f)
        {
            obj->on_ground = true;
        }

        obj->bounds_dirty = true;
    }
}

/*
 * Public API
 */

void voxel_body_world_update(VoxelObjectWorld *world, float dt)
{
    bool needs_cleanup = false;
    VoxelVolume *terrain = world->terrain;

    /* Update each object */
    for (int32_t i = 0; i < world->object_count; i++)
    {
        VoxelObject *obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0)
            continue;

        /* Initialize inertia tensor on first frame (check diagonal element) */
        if (obj->inv_inertia_local[0] == 0.0f && obj->mass > 0.0f)
        {
            rigid_body_compute_inertia(obj);
        }

        /* Update lifetime */
        obj->lifetime += dt;

        /* Skip sleeping objects (but still update lifetime for cleanup) */
        if (obj->sleeping)
        {
            /* Check if something external woke us */
            float speed = vec3_length(obj->velocity);
            if (speed > VOBJ_WAKE_VELOCITY_THRESHOLD)
            {
                obj->sleeping = false;
                obj->settle_timer = 0.0f;
            }
            else
            {
                continue;
            }
        }

        /* Update world-space inertia tensor */
        rigid_body_update_inertia(obj);

        /* Apply gravity with Pade damping */
        obj->velocity = vec3_add(obj->velocity, vec3_scale(world->gravity, dt));

        /* Apply topple torque using inertia-aware method */
        apply_topple_torque(obj, &world->bounds, dt);

        /* Per-object velocity clamp based on radius to prevent tunneling */
        float max_velocity = obj->radius * 0.4f / dt;
        if (max_velocity > 30.0f) max_velocity = 30.0f;
        clamp_velocity(&obj->velocity, max_velocity);

        /* Clamp angular velocity */
        float ang_speed = vec3_length(obj->angular_velocity);
        if (ang_speed > 15.0f)
        {
            obj->angular_velocity = vec3_scale(obj->angular_velocity, 15.0f / ang_speed);
        }

        /* Pade damping - stable at any timestep (from qu3e/Box2D) */
        float linear_damp = 1.0f - world->damping;
        float angular_damp = 1.0f - world->angular_damping;
        float linear_factor = 1.0f / (1.0f + dt * linear_damp);
        float angular_factor = 1.0f / (1.0f + dt * angular_damp);
        obj->velocity = vec3_scale(obj->velocity, linear_factor);
        obj->angular_velocity = vec3_scale(obj->angular_velocity, angular_factor);

        /* Additional settling behavior on ground */
        if (obj->on_ground)
        {
            float speed = vec3_length(obj->velocity);
            if (speed < VOBJ_SETTLING_VELOCITY)
            {
                if (speed < 0.08f)
                {
                    obj->velocity.x = 0.0f;
                    obj->velocity.z = 0.0f;
                }
                float local_ang_speed = vec3_length(obj->angular_velocity);
                if (local_ang_speed < 0.2f)
                {
                    obj->angular_velocity = vec3_zero();
                }
            }
        }

        /* Pre-integration floor sweep check (CCD) - use half-extents for accuracy */
        float floor_y = world->bounds.min_y;
        float half_height = obj->shape_half_extents.y;
        float approx_lowest_y = obj->position.y + obj->center_of_mass_offset.y - half_height;
        float floor_clearance = approx_lowest_y - floor_y;
        float y_movement = obj->velocity.y * dt;

        if (y_movement < 0.0f && floor_clearance + y_movement < 0.0f)
        {
            if (floor_clearance > 0.01f)
            {
                obj->velocity.y = -floor_clearance / dt * 0.95f;
            }
            else if (floor_clearance > -0.01f)
            {
                obj->velocity.y = 0.0f;
            }
        }

        /* Integrate position using velocity */
        obj->position = vec3_add(obj->position, vec3_scale(obj->velocity, dt));

        /* Integrate orientation using quaternion (proper rotation integration) */
        quat_integrate(&obj->orientation, obj->angular_velocity, dt);
        obj->orientation = quat_normalize(obj->orientation);

        /* Mark bounds dirty after transform changes */
        obj->bounds_dirty = true;

        /* Ground/wall collision (single pass - penetration resolution is built-in) */
        resolve_rotated_ground_collision(obj, &world->bounds,
                                          world->restitution, world->floor_friction);

        /* Terrain collision (if terrain is set) */
        if (terrain)
        {
            resolve_terrain_collision(obj, terrain, world->restitution, world->floor_friction);
        }

        /* Hard safety clamp - ensure object never falls through floor */
        float post_lowest_y = obj->position.y + obj->center_of_mass_offset.y - half_height;
        if (post_lowest_y < floor_y - 0.05f)
        {
            /* Object fell through - push it back up */
            float correction = floor_y - post_lowest_y + 0.02f;
            obj->position.y += correction;
            if (obj->velocity.y < 0.0f)
                obj->velocity.y = 0.0f;
            obj->on_ground = true;
            obj->bounds_dirty = true;
        }

        /* Sleep detection */
        float speed = vec3_length(obj->velocity);
        ang_speed = vec3_length(obj->angular_velocity);

        if (speed < VOBJ_SLEEP_VELOCITY_THRESHOLD &&
            ang_speed < VOBJ_SLEEP_ANGULAR_THRESHOLD &&
            obj->on_ground)
        {
            obj->settle_timer += dt;
            if (obj->settle_timer >= VOBJ_SLEEP_TIME_REQUIRED)
            {
                obj->sleeping = true;
                obj->velocity = vec3_zero();
                obj->angular_velocity = vec3_zero();
            }
        }
        else
        {
            obj->settle_timer = 0.0f;
        }

        /* Only deactivate objects that fall far out of bounds */
        if (obj->position.y < world->bounds.min_y - VOBJ_OUT_OF_BOUNDS_MARGIN)
        {
            obj->active = false;
            needs_cleanup = true;
        }
    }

    /* Object-object collisions using spatial hash: O(n) average */
    PROFILE_BEGIN(PROFILE_SIM_COLLISION);
    if (world->enable_object_collision)
    {
        spatial_hash_clear(&world->collision_grid);

        /* Insert active non-sleeping objects into grid */
        for (int32_t i = 0; i < world->object_count; i++)
        {
            VoxelObject *obj = &world->objects[i];
            if (!obj->active)
                continue;

            Vec3 center = vec3_add(obj->position, obj->center_of_mass_offset);
            spatial_hash_insert(&world->collision_grid, i, center, obj->radius);
        }

        /* Check collisions using spatial hash */
        for (int32_t i = 0; i < world->object_count; i++)
        {
            VoxelObject *obj = &world->objects[i];
            if (!obj->active)
                continue;

            Vec3 center = vec3_add(obj->position, obj->center_of_mass_offset);
            int32_t nearby[SPATIAL_HASH_MAX_PER_CELL];
            int32_t nearby_count = spatial_hash_query(&world->collision_grid,
                center, obj->radius * 2.0f,
                nearby, SPATIAL_HASH_MAX_PER_CELL);

            for (int32_t n = 0; n < nearby_count; n++)
            {
                int32_t j = nearby[n];
                if (j <= i) continue;
                if (!world->objects[j].active)
                    continue;

                resolve_object_collision(&world->objects[i], &world->objects[j],
                                          world->restitution);
            }
        }
    }
    PROFILE_END(PROFILE_SIM_COLLISION);

    /* Post-collision ground enforcement: prevents stacked objects from clipping through floor */
    for (int32_t iter = 0; iter < COLLISION_GROUND_ITERATIONS; iter++)
    {
        bool any_correction = false;

        for (int32_t i = 0; i < world->object_count; i++)
        {
            VoxelObject *obj = &world->objects[i];
            if (!obj->active)
                continue;

            ensure_cached_bounds(obj);
            float lowest_y = obj->cached_lowest_y;
            float floor_y = world->bounds.min_y;

            if (lowest_y < floor_y)
            {
                obj->position.y += (floor_y - lowest_y);
                if (obj->velocity.y < 0.0f)
                    obj->velocity.y = 0.0f;
                obj->on_ground = true;
                obj->bounds_dirty = true;
                any_correction = true;
            }
        }

        if (!any_correction)
            break;
    }

    /* Compact array if any objects were deactivated */
    if (needs_cleanup)
    {
        cleanup_inactive_objects(world);
    }
}

void voxel_body_world_update_with_terrain(VoxelObjectWorld *world, VoxelVolume *terrain, float dt)
{
    /* Set terrain and delegate to main update (terrain collision is now automatic) */
    world->terrain = terrain;
    voxel_body_world_update(world, dt);
}
