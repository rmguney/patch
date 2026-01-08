#ifndef PATCH_CORE_VOXEL_PHYSICS_H
#define PATCH_CORE_VOXEL_PHYSICS_H

#include "types.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Voxel* voxels;
    int32_t grid_x;
    int32_t grid_y;
    int32_t grid_z;
    
    Bounds3D bounds;
    float voxel_size;
    
    uint32_t frame_counter;
} VoxelWorld;

VoxelWorld* voxel_world_create(Bounds3D bounds);
void voxel_world_destroy(VoxelWorld* world);
void voxel_world_update(VoxelWorld* world);
void voxel_world_clear(VoxelWorld* world);

int32_t voxel_world_index(const VoxelWorld* world, int32_t x, int32_t y, int32_t z);
bool voxel_world_in_bounds(const VoxelWorld* world, int32_t x, int32_t y, int32_t z);
void voxel_world_set(VoxelWorld* world, int32_t x, int32_t y, int32_t z, uint8_t r, uint8_t g, uint8_t b);
Voxel voxel_world_get(const VoxelWorld* world, int32_t x, int32_t y, int32_t z);

void voxel_world_spawn_sphere(VoxelWorld* world, Vec3 center, float radius, Vec3 color);
void voxel_world_spawn_explosion(VoxelWorld* world, Vec3 center, float radius, int32_t count, Vec3 color);

void voxel_world_to_grid(const VoxelWorld* world, Vec3 pos, int32_t* out_x, int32_t* out_y, int32_t* out_z);
Vec3 voxel_world_to_world(const VoxelWorld* world, int32_t x, int32_t y, int32_t z);

#ifdef __cplusplus
}
#endif

#endif
