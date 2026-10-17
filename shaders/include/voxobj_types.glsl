#ifndef VOXOBJ_TYPES_GLSL
#define VOXOBJ_TYPES_GLSL

/* Brick atlas layout (engine/render/voxel_bricks.h): objects are 4x4x4 bricks of 8^3 voxels
 * from their brick offset on, the atlas a 32x32x32 grid of bricks; brick and atlas brick
 * indices run x fastest */
#define VOBJ_BRICK_SIZE 8
#define VOBJ_BRICKS_PER_AXIS 4
#define BRICK_ATLAS_BRICKS_PER_AXIS 32

/* Mirror of patch::VoxelObjectGPU (engine/render/voxel_bricks.h), std430, 384 bytes */
struct VoxelObjectGPU {
    mat4 world_to_local;   /* rotation + translation, no scale */
    mat4 local_to_world;   /* rotation * voxel_size + translation */
    vec4 bounds_min;       /* xyz: local AABB min, w: voxel_size */
    vec4 bounds_max;       /* xyz: local AABB max, w: grid_size */
    vec4 position;         /* xyz: world position, w: 1 = visible */
    uvec2 brick_mask;      /* bit b (x: bricks 0-31, y: 32-63): brick b is in the atlas */
    uint brick_offset;     /* grid cell where the bricks start: x | y << 8 | z << 16 */
    uint pad;
    uint brick_table[32];  /* atlas brick of brick b: 16 bits at (b & 1) * 16 of entry b / 2 */
    mat4 prev_local_to_world; /* local_to_world of the previous frame (motion vectors) */
};

#endif /* VOXOBJ_TYPES_GLSL */
