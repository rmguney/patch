#include "physics_step.h"
#include "broadphase.h"
#include "volume_contact.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FRAGMENT_VOXEL_STORAGE_SIZE (256 * 1024) /* 256KB for fragment voxels */

void physics_state_init(PhysicsState *state, Bounds3D bounds, VoxelVolume *volume)
{
    memset(state, 0, sizeof(PhysicsState));

    state->bounds = bounds;
    state->volume = volume;
    state->gravity = vec3_create(0.0f, -18.0f, 0.0f);
    state->damping = 0.98f;
    state->floor_y = bounds.min_y;
    state->current_frame = 0;

    /* Initialize proxy free list (all slots available) */
    state->proxy_free_count = PHYSICS_PROXY_MAX;
    for (int32_t i = 0; i < PHYSICS_PROXY_MAX; i++)
    {
        state->proxy_free_list[i] = PHYSICS_PROXY_MAX - 1 - i; /* Stack order */
    }

    /* Preallocate fragment voxel storage */
    state->fragment_voxel_storage = (uint8_t *)calloc(1, FRAGMENT_VOXEL_STORAGE_SIZE);
    state->fragment_voxel_storage_size = FRAGMENT_VOXEL_STORAGE_SIZE;
    state->fragment_voxel_storage_used = 0;
}

void physics_state_destroy(PhysicsState *state)
{
    if (state->fragment_voxel_storage)
    {
        free(state->fragment_voxel_storage);
        state->fragment_voxel_storage = NULL;
    }
}

/* O(1) proxy allocation using free list */
int32_t physics_proxy_alloc(PhysicsState *state)
{
    if (state->proxy_free_count == 0)
        return -1;

    /* Pop from free list */
    int32_t i = state->proxy_free_list[--state->proxy_free_count];

    memset(&state->proxies[i], 0, sizeof(PhysicsProxy));
    state->proxies[i].active = true;
    state->proxies[i].mass = 1.0f;
    state->proxies[i].restitution = 0.3f;
    state->proxies[i].friction = 0.5f;
    state->proxy_count++;
    return i;
}

/* O(1) proxy deallocation using free list */
void physics_proxy_free(PhysicsState *state, int32_t index)
{
    if (index >= 0 && index < PHYSICS_PROXY_MAX && state->proxies[index].active)
    {
        state->proxies[index].active = false;
        state->proxy_count--;
        /* Push to free list */
        state->proxy_free_list[state->proxy_free_count++] = index;
    }
}

PhysicsProxy *physics_proxy_get(PhysicsState *state, int32_t index)
{
    if (index >= 0 && index < PHYSICS_PROXY_MAX && state->proxies[index].active)
    {
        return &state->proxies[index];
    }
    return NULL;
}

static void physics_step_proxy(PhysicsState *state, PhysicsProxy *proxy, float dt)
{
    if (proxy->flags & PROXY_FLAG_STATIC)
        return;

    if (proxy->flags & PROXY_FLAG_KINEMATIC)
        return;

    /* Apply gravity */
    if (proxy->flags & PROXY_FLAG_GRAVITY)
    {
        proxy->velocity = vec3_add(proxy->velocity, vec3_scale(state->gravity, dt));
    }

    /* Integrate position */
    Vec3 new_pos = vec3_add(proxy->position, vec3_scale(proxy->velocity, dt));

    /* Voxel collision */
    if ((proxy->flags & PROXY_FLAG_COLLIDE_VOXEL) && state->volume)
    {
        VoxelContactResult contacts;

        if (proxy->shape == PROXY_SHAPE_SPHERE)
        {
            float radius = proxy->half_extents.x;
            volume_contact_sphere(state->volume, new_pos, radius, &contacts);
        }
        else if (proxy->shape == PROXY_SHAPE_AABB)
        {
            Vec3 min_corner = vec3_sub(new_pos, proxy->half_extents);
            Vec3 max_corner = vec3_add(new_pos, proxy->half_extents);
            volume_contact_aabb(state->volume, min_corner, max_corner, &contacts);
        }
        else if (proxy->shape == PROXY_SHAPE_CAPSULE)
        {
            /* Capsule: half_extents.x = radius, half_extents.y = half-height */
            float radius = proxy->half_extents.x;
            float half_h = proxy->half_extents.y;
            Vec3 p0 = vec3_create(new_pos.x, new_pos.y - half_h, new_pos.z);
            Vec3 p1 = vec3_create(new_pos.x, new_pos.y + half_h, new_pos.z);
            volume_contact_capsule(state->volume, p0, p1, radius, &contacts);
        }
        else
        {
            contacts.count = 0;
            contacts.any_contact = false;
        }

        if (contacts.any_contact)
        {
            Vec3 push = volume_contact_resolve(&contacts);
            new_pos = vec3_add(new_pos, push);

            /* Reflect velocity along contact normal */
            if (contacts.max_depth > 0.001f)
            {
                Vec3 normal = vec3_normalize(contacts.average_normal);
                float vn = vec3_dot(proxy->velocity, normal);
                if (vn < 0.0f)
                {
                    Vec3 vn_vec = vec3_scale(normal, vn);
                    proxy->velocity = vec3_sub(proxy->velocity, vec3_scale(vn_vec, 1.0f + proxy->restitution));

                    /* Apply friction */
                    Vec3 vt = vec3_sub(proxy->velocity, vec3_scale(normal, vec3_dot(proxy->velocity, normal)));
                    proxy->velocity = vec3_sub(proxy->velocity, vec3_scale(vt, proxy->friction * dt * 10.0f));
                }

                /* Check if grounded (contact normal pointing up) */
                proxy->grounded = (normal.y > 0.7f);
            }
        }
        else
        {
            proxy->grounded = false;
        }
    }

    /* Floor collision */
    float bottom_offset;
    if (proxy->shape == PROXY_SHAPE_SPHERE)
    {
        bottom_offset = proxy->half_extents.x;
    }
    else if (proxy->shape == PROXY_SHAPE_CAPSULE)
    {
        bottom_offset = proxy->half_extents.y + proxy->half_extents.x; /* half-height + radius */
    }
    else
    {
        bottom_offset = proxy->half_extents.y;
    }

    if (new_pos.y - bottom_offset < state->floor_y)
    {
        new_pos.y = state->floor_y + bottom_offset;
        if (proxy->velocity.y < 0.0f)
        {
            proxy->velocity.y = -proxy->velocity.y * proxy->restitution;
        }
        proxy->grounded = true;
    }

    /* Apply damping */
    proxy->velocity = vec3_scale(proxy->velocity, state->damping);

    /* Clamp velocity based on proxy size to prevent tunneling */
    float min_extent = fminf(proxy->half_extents.x,
                             fminf(proxy->half_extents.y, proxy->half_extents.z));
    float max_velocity = min_extent / dt;
    if (max_velocity < 5.0f) max_velocity = 5.0f;
    if (max_velocity > 50.0f) max_velocity = 50.0f;

    float speed = vec3_length(proxy->velocity);
    if (speed > max_velocity)
    {
        proxy->velocity = vec3_scale(proxy->velocity, max_velocity / speed);
    }
    if (speed < 0.01f)
    {
        proxy->velocity = vec3_zero();
    }

    proxy->position = new_pos;
}

/* Resolve collision between two sphere proxies */
static void resolve_proxy_sphere_collision(PhysicsProxy *a, PhysicsProxy *b)
{
    Vec3 delta = vec3_sub(b->position, a->position);
    float dist_sq = vec3_length_sq(delta);
    float sum_radius = a->half_extents.x + b->half_extents.x;

    if (dist_sq >= sum_radius * sum_radius || dist_sq < 0.0001f)
        return;

    float dist = sqrtf(dist_sq);
    Vec3 normal = vec3_scale(delta, 1.0f / dist);
    float penetration = sum_radius - dist;

    /* Push apart based on relative mass */
    float total_mass = a->mass + b->mass;
    if (total_mass < 0.001f)
        total_mass = 1.0f;
    float a_ratio = b->mass / total_mass;
    float b_ratio = a->mass / total_mass;

    a->position = vec3_sub(a->position, vec3_scale(normal, penetration * a_ratio));
    b->position = vec3_add(b->position, vec3_scale(normal, penetration * b_ratio));

    /* Compute relative velocity along collision normal */
    Vec3 rel_vel = vec3_sub(b->velocity, a->velocity);
    float vel_along_normal = vec3_dot(rel_vel, normal);

    if (vel_along_normal > 0.0f)
        return; /* Moving apart */

    /* Compute impulse with restitution */
    float restitution = (a->restitution + b->restitution) * 0.5f;
    float impulse_mag = -(1.0f + restitution) * vel_along_normal;
    impulse_mag /= (1.0f / a->mass + 1.0f / b->mass);

    Vec3 impulse = vec3_scale(normal, impulse_mag);
    a->velocity = vec3_sub(a->velocity, vec3_scale(impulse, 1.0f / a->mass));
    b->velocity = vec3_add(b->velocity, vec3_scale(impulse, 1.0f / b->mass));
}

/* O(n²) fallback for small proxy counts */
static void physics_resolve_proxy_collisions_bruteforce(PhysicsState *state)
{
    for (int32_t i = 0; i < PHYSICS_PROXY_MAX; i++)
    {
        PhysicsProxy *a = &state->proxies[i];
        if (!a->active || !(a->flags & PROXY_FLAG_COLLIDE_PROXY))
            continue;

        for (int32_t j = i + 1; j < PHYSICS_PROXY_MAX; j++)
        {
            PhysicsProxy *b = &state->proxies[j];
            if (!b->active || !(b->flags & PROXY_FLAG_COLLIDE_PROXY))
                continue;

            if (a->shape == PROXY_SHAPE_SPHERE && b->shape == PROXY_SHAPE_SPHERE)
            {
                resolve_proxy_sphere_collision(a, b);
            }
        }
    }
}

/* Broadphase-accelerated collision for large proxy counts */
static void physics_resolve_proxy_collisions_broadphase(PhysicsState *state)
{
    /* Thread-local broadphase grid (static to avoid stack allocation each frame) */
    static BroadphaseGrid grid;
    broadphase_init(&grid, state->bounds);

    /* Insert all collidable proxies */
    for (int32_t i = 0; i < PHYSICS_PROXY_MAX; i++)
    {
        PhysicsProxy *p = &state->proxies[i];
        if (!p->active || !(p->flags & PROXY_FLAG_COLLIDE_PROXY))
            continue;

        float radius = (p->shape == PROXY_SHAPE_SPHERE) ? p->half_extents.x : vec3_length(p->half_extents);
        broadphase_insert(&grid, (uint16_t)i, p->position, radius);
    }

    /* Generate and sort pairs */
    broadphase_generate_pairs(&grid);
    broadphase_sort_pairs(&grid);

    /* Resolve each pair */
    for (int32_t i = 0; i < grid.pair_count; i++)
    {
        PhysicsProxy *a = &state->proxies[grid.pairs[i].a];
        PhysicsProxy *b = &state->proxies[grid.pairs[i].b];

        if (a->shape == PROXY_SHAPE_SPHERE && b->shape == PROXY_SHAPE_SPHERE)
        {
            resolve_proxy_sphere_collision(a, b);
        }
    }
}

/* Resolve all proxy-proxy collisions (uses broadphase when count exceeds threshold) */
static void physics_resolve_proxy_collisions(PhysicsState *state)
{
    if (state->proxy_count < PHYSICS_BROADPHASE_THRESHOLD)
    {
        physics_resolve_proxy_collisions_bruteforce(state);
    }
    else
    {
        physics_resolve_proxy_collisions_broadphase(state);
    }
}

static void physics_step_fragment(PhysicsState *state, VoxelFragment *frag, float dt)
{
    if (!frag->active)
        return;

    /* Apply gravity */
    frag->velocity = vec3_add(frag->velocity, vec3_scale(state->gravity, dt));

    /* Integrate position */
    Vec3 new_pos = vec3_add(frag->position, vec3_scale(frag->velocity, dt));

    /* Integrate rotation (full 3-axis) */
    frag->rotation += frag->angular_velocity.y * dt; /* Primary Y-axis still tracked separately for renderer */

    /* Approximate bounding sphere radius */
    float half_x = (float)frag->size_x * frag->voxel_size * 0.5f;
    float half_y = (float)frag->size_y * frag->voxel_size * 0.5f;
    float half_z = (float)frag->size_z * frag->voxel_size * 0.5f;
    float bounding_radius = sqrtf(half_x * half_x + half_y * half_y + half_z * half_z);

    /* Volume collision (approximate with sphere) */
    if (state->volume)
    {
        VoxelContactResult contacts;
        volume_contact_sphere(state->volume, new_pos, bounding_radius * 0.7f, &contacts);

        if (contacts.any_contact)
        {
            Vec3 push = volume_contact_resolve(&contacts);
            new_pos = vec3_add(new_pos, push);

            /* Reflect velocity */
            if (contacts.max_depth > 0.001f)
            {
                Vec3 normal = vec3_normalize(contacts.average_normal);
                float vn = vec3_dot(frag->velocity, normal);
                if (vn < 0.0f)
                {
                    frag->velocity = vec3_sub(frag->velocity, vec3_scale(normal, vn * 1.3f));
                    /* Add spin from impact */
                    Vec3 tangent = vec3_cross(normal, frag->velocity);
                    float tangent_len = vec3_length(tangent);
                    if (tangent_len > 0.01f)
                    {
                        frag->angular_velocity = vec3_add(frag->angular_velocity,
                                                          vec3_scale(tangent, contacts.max_depth * 2.0f));
                    }
                }
            }
        }
    }

    /* Floor collision */
    if (new_pos.y - half_y < state->floor_y)
    {
        new_pos.y = state->floor_y + half_y;
        if (frag->velocity.y < 0.0f)
        {
            frag->velocity.y = -frag->velocity.y * 0.3f;

            /* Add horizontal angular velocity from floor impact */
            float horiz_speed = sqrtf(frag->velocity.x * frag->velocity.x +
                                      frag->velocity.z * frag->velocity.z);
            if (horiz_speed > 0.1f)
            {
                frag->angular_velocity.x += frag->velocity.z * 0.5f;
                frag->angular_velocity.z -= frag->velocity.x * 0.5f;
            }
        }
    }

    frag->position = new_pos;

    /* Apply damping */
    frag->velocity = vec3_scale(frag->velocity, state->damping);
    frag->angular_velocity = vec3_scale(frag->angular_velocity, 0.96f);

    /* Clamp velocity */
    float speed = vec3_length(frag->velocity);
    if (speed > 50.0f)
    {
        frag->velocity = vec3_scale(frag->velocity, 50.0f / speed);
    }

    /* Clamp angular velocity */
    float ang_speed = vec3_length(frag->angular_velocity);
    if (ang_speed > 15.0f)
    {
        frag->angular_velocity = vec3_scale(frag->angular_velocity, 15.0f / ang_speed);
    }

    /* Sleep check */
    if (speed < 0.1f && ang_speed < 0.1f &&
        frag->position.y - half_y < state->floor_y + 0.1f)
    {
        frag->velocity = vec3_zero();
        frag->angular_velocity = vec3_zero();
    }
}

void physics_step(PhysicsState *state, float dt, RngState *rng)
{
    (void)rng; /* Reserved for future use (randomized fragment spawning) */

    state->current_frame++;

    /* Step all proxies */
    for (int32_t i = 0; i < PHYSICS_PROXY_MAX; i++)
    {
        if (state->proxies[i].active)
        {
            physics_step_proxy(state, &state->proxies[i], dt);
        }
    }

    /* Resolve proxy-proxy collisions */
    physics_resolve_proxy_collisions(state);

    /* Step all fragments */
    for (int32_t i = 0; i < PHYSICS_FRAGMENT_MAX; i++)
    {
        if (state->fragments[i].active)
        {
            physics_step_fragment(state, &state->fragments[i], dt);
        }
    }
}

int32_t physics_fragment_spawn(PhysicsState *state, const uint8_t *voxels,
                               int32_t size_x, int32_t size_y, int32_t size_z,
                               Vec3 world_origin, float voxel_size, Vec3 initial_velocity)
{
    /* Find free fragment slot */
    int32_t slot = -1;
    for (int32_t i = 0; i < PHYSICS_FRAGMENT_MAX; i++)
    {
        if (!state->fragments[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    /* Calculate storage needed */
    int32_t voxel_count = size_x * size_y * size_z;
    if (state->fragment_voxel_storage_used + voxel_count > state->fragment_voxel_storage_size)
    {
        return -1; /* Out of voxel storage */
    }

    VoxelFragment *frag = &state->fragments[slot];
    memset(frag, 0, sizeof(VoxelFragment));

    /* Copy voxel data */
    frag->voxels = state->fragment_voxel_storage + state->fragment_voxel_storage_used;
    memcpy(frag->voxels, voxels, (size_t)voxel_count);
    state->fragment_voxel_storage_used += voxel_count;

    frag->size_x = size_x;
    frag->size_y = size_y;
    frag->size_z = size_z;
    frag->voxel_size = voxel_size;

    /* Calculate center of mass and solid count */
    Vec3 com_sum = vec3_zero();
    int32_t solid = 0;
    for (int32_t z = 0; z < size_z; z++)
    {
        for (int32_t y = 0; y < size_y; y++)
        {
            for (int32_t x = 0; x < size_x; x++)
            {
                int32_t idx = x + y * size_x + z * size_x * size_y;
                if (voxels[idx] != 0)
                {
                    com_sum.x += (float)x + 0.5f;
                    com_sum.y += (float)y + 0.5f;
                    com_sum.z += (float)z + 0.5f;
                    solid++;
                }
            }
        }
    }

    frag->solid_count = solid;
    if (solid > 0)
    {
        frag->local_com = vec3_scale(com_sum, 1.0f / (float)solid);
    }
    else
    {
        frag->local_com = vec3_create((float)size_x * 0.5f, (float)size_y * 0.5f, (float)size_z * 0.5f);
    }

    /* Set world position at center of mass */
    frag->position = vec3_add(world_origin, vec3_scale(frag->local_com, voxel_size));
    frag->velocity = initial_velocity;
    frag->angular_velocity = vec3_zero();
    frag->rotation = 0.0f;
    frag->mass = (float)solid * 0.1f; /* Simple mass = voxel count * base mass */
    frag->spawn_frame = state->current_frame;
    frag->active = true;

    state->fragment_count++;
    return slot;
}

VoxelFragment *physics_fragment_get(PhysicsState *state, int32_t index)
{
    if (index >= 0 && index < PHYSICS_FRAGMENT_MAX && state->fragments[index].active)
    {
        return &state->fragments[index];
    }
    return NULL;
}

void physics_fragment_free(PhysicsState *state, int32_t index)
{
    if (index >= 0 && index < PHYSICS_FRAGMENT_MAX && state->fragments[index].active)
    {
        state->fragments[index].active = false;
        state->fragment_count--;

        /*
         * Storage reclamation policy:
         * - If no fragments are active, reset the allocator completely
         * - This handles the common case of periodic destruction/respawn cycles
         * - Fragments in the middle are not reclaimed (would require compaction)
         */
        if (state->fragment_count == 0)
        {
            state->fragment_voxel_storage_used = 0;
        }
    }
}
