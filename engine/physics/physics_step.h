#ifndef PATCH_PHYSICS_STEP_H
#define PATCH_PHYSICS_STEP_H

#include "engine/core/types.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Physics Step
 *
 * Operates on PhysicsProxy objects that represent game entities without knowing
 * their gameplay types.
 *
 * - Voxel collision uses volume contact sampling
 * - Fragment spawning is bounded per tick (VOLUME_MAX_FRAGMENTS_PER_TICK)
 * - No heap allocations during step
 */

#define PHYSICS_PROXY_MAX 2048
#define PHYSICS_FRAGMENT_MAX 1024

/* Broadphase threshold: use O(n²) below this, broadphase above */
#define PHYSICS_BROADPHASE_THRESHOLD 32

/* Proxy flags for collision behavior */
typedef enum
{
    PROXY_FLAG_NONE = 0,
    PROXY_FLAG_STATIC = (1 << 0),       /* Does not move */
    PROXY_FLAG_KINEMATIC = (1 << 1),    /* Moved by game, not physics */
    PROXY_FLAG_GRAVITY = (1 << 2),      /* Affected by gravity */
    PROXY_FLAG_COLLIDE_VOXEL = (1 << 3), /* Collides with voxel volume */
    PROXY_FLAG_COLLIDE_PROXY = (1 << 4), /* Collides with other proxies */
} PhysicsProxyFlags;

/* Shape type for collision */
typedef enum
{
    PROXY_SHAPE_SPHERE,
    PROXY_SHAPE_AABB,
    PROXY_SHAPE_CAPSULE,
} PhysicsProxyShape;

/*
 * PhysicsProxy: represents any collidable object without knowing its gameplay type.
 * Game code allocates proxies and reads back position/velocity after physics step.
 */
typedef struct
{
    Vec3 position;
    Vec3 velocity;
    Vec3 half_extents;   /* For AABB; radius stored in half_extents.x for sphere */
    float mass;
    float restitution;
    float friction;
    uint32_t flags;
    PhysicsProxyShape shape;
    uint32_t user_id;    /* Game-defined ID to map back to entities */
    bool active;
    bool grounded;       /* True if resting on voxel surface */
    uint8_t _pad[2];
} PhysicsProxy;

/*
 * VoxelFragment: a detached piece of voxel volume that moves independently.
 * Created when connectivity detection finds floating islands.
 */
typedef struct
{
    Vec3 position;       /* Center of mass in world space */
    Vec3 velocity;
    Vec3 angular_velocity;
    float rotation;      /* Simplified: rotation around Y axis only */

    /* Voxel data: stored as flat array of material IDs */
    uint8_t *voxels;     /* Pointer to voxel data (preallocated) */
    int32_t size_x, size_y, size_z;
    int32_t solid_count;
    float voxel_size;

    Vec3 local_com;      /* Center of mass in local voxel space */
    float mass;

    uint32_t spawn_frame;
    bool active;
    uint8_t _pad[3];
} VoxelFragment;

/*
 * PhysicsWorld: owns all physics state for a scene.
 */
typedef struct
{
    PhysicsProxy proxies[PHYSICS_PROXY_MAX];
    int32_t proxy_count;

    /* Free list for O(1) proxy allocation */
    int32_t proxy_free_list[PHYSICS_PROXY_MAX];
    int32_t proxy_free_count;

    VoxelFragment fragments[PHYSICS_FRAGMENT_MAX];
    int32_t fragment_count;

    /* Fragment voxel storage (preallocated flat buffer) */
    uint8_t *fragment_voxel_storage;
    int32_t fragment_voxel_storage_size;
    int32_t fragment_voxel_storage_used;

    VoxelVolume *volume;  /* Reference to voxel volume for collision */
    Bounds3D bounds;
    Vec3 gravity;

    float damping;
    float floor_y;

    uint32_t current_frame;
} PhysicsState;

/* Initialize physics state */
void physics_state_init(PhysicsState *state, Bounds3D bounds, VoxelVolume *volume);

/* Cleanup physics state */
void physics_state_destroy(PhysicsState *state);

/* Allocate a physics proxy, returns index or -1 if full */
int32_t physics_proxy_alloc(PhysicsState *state);

/* Free a physics proxy */
void physics_proxy_free(PhysicsState *state, int32_t index);

/* Get proxy by index (NULL if invalid) */
PhysicsProxy *physics_proxy_get(PhysicsState *state, int32_t index);

/* Run one physics step (fixed dt) */
void physics_step(PhysicsState *state, float dt, RngState *rng);

/* Spawn a fragment from voxel data (returns fragment index or -1) */
int32_t physics_fragment_spawn(PhysicsState *state, const uint8_t *voxels,
                               int32_t size_x, int32_t size_y, int32_t size_z,
                               Vec3 world_origin, float voxel_size, Vec3 initial_velocity);

/* Get fragment by index (NULL if invalid) */
VoxelFragment *physics_fragment_get(PhysicsState *state, int32_t index);

/* Free a fragment */
void physics_fragment_free(PhysicsState *state, int32_t index);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_PHYSICS_STEP_H */
