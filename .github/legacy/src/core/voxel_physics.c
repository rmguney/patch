#include "voxel_physics.h"
#include <stdlib.h>
#include <string.h>

VoxelWorld* voxel_world_create(Bounds3D bounds) {
    VoxelWorld* world = (VoxelWorld*)calloc(1, sizeof(VoxelWorld));
    if (!world) {
        return NULL;
    }
    
    world->grid_x = VOXEL_GRID_X;
    world->grid_y = VOXEL_GRID_Y;
    world->grid_z = VOXEL_GRID_Z;
    world->bounds = bounds;
    
    float width = bounds.max_x - bounds.min_x;
    float depth = bounds.max_z - bounds.min_z;
    float max_dim = width > depth ? width : depth;
    world->voxel_size = max_dim / (float)VOXEL_GRID_X;
    
    size_t total = (size_t)world->grid_x * (size_t)world->grid_y * (size_t)world->grid_z;
    world->voxels = (Voxel*)calloc(total, sizeof(Voxel));
    if (!world->voxels) {
        free(world);
        return NULL;
    }
    
    world->frame_counter = 0;
    
    return world;
}

void voxel_world_destroy(VoxelWorld* world) {
    if (world) {
        free(world->voxels);
        free(world);
    }
}

int32_t voxel_world_index(const VoxelWorld* world, int32_t x, int32_t y, int32_t z) {
    return x + y * world->grid_x + z * world->grid_x * world->grid_y;
}

bool voxel_world_in_bounds(const VoxelWorld* world, int32_t x, int32_t y, int32_t z) {
    return x >= 0 && x < world->grid_x &&
           y >= 0 && y < world->grid_y &&
           z >= 0 && z < world->grid_z;
}

void voxel_world_set(VoxelWorld* world, int32_t x, int32_t y, int32_t z, uint8_t r, uint8_t g, uint8_t b) {
    if (!voxel_world_in_bounds(world, x, y, z)) {
        return;
    }
    int32_t idx = voxel_world_index(world, x, y, z);
    world->voxels[idx].r = r;
    world->voxels[idx].g = g;
    world->voxels[idx].b = b;
    world->voxels[idx].active = 1;
}

Voxel voxel_world_get(const VoxelWorld* world, int32_t x, int32_t y, int32_t z) {
    if (!voxel_world_in_bounds(world, x, y, z)) {
        Voxel empty = {0, 0, 0, 0};
        return empty;
    }
    return world->voxels[voxel_world_index(world, x, y, z)];
}

void voxel_world_to_grid(const VoxelWorld* world, Vec3 pos, int32_t* out_x, int32_t* out_y, int32_t* out_z) {
    *out_x = (int32_t)((pos.x - world->bounds.min_x) / world->voxel_size);
    *out_y = (int32_t)((pos.y - world->bounds.min_y) / world->voxel_size);
    *out_z = (int32_t)((pos.z - world->bounds.min_z) / world->voxel_size);
}

Vec3 voxel_world_to_world(const VoxelWorld* world, int32_t x, int32_t y, int32_t z) {
    return vec3_create(
        world->bounds.min_x + ((float)x + 0.5f) * world->voxel_size,
        world->bounds.min_y + ((float)y + 0.5f) * world->voxel_size,
        world->bounds.min_z + ((float)z + 0.5f) * world->voxel_size
    );
}

void voxel_world_clear(VoxelWorld* world) {
    size_t total = (size_t)world->grid_x * (size_t)world->grid_y * (size_t)world->grid_z;
    memset(world->voxels, 0, total * sizeof(Voxel));
}

static void swap_voxels(VoxelWorld* world, int32_t x1, int32_t y1, int32_t z1, int32_t x2, int32_t y2, int32_t z2) {
    int32_t idx1 = voxel_world_index(world, x1, y1, z1);
    int32_t idx2 = voxel_world_index(world, x2, y2, z2);
    Voxel tmp = world->voxels[idx1];
    world->voxels[idx1] = world->voxels[idx2];
    world->voxels[idx2] = tmp;
}

void voxel_world_update(VoxelWorld* world) {
    world->frame_counter++;
    bool even_frame = (world->frame_counter % 2) == 0;
    
    int32_t x_start = even_frame ? 0 : world->grid_x - 1;
    int32_t x_end = even_frame ? world->grid_x : -1;
    int32_t x_step = even_frame ? 1 : -1;
    
    int32_t z_start = even_frame ? 0 : world->grid_z - 1;
    int32_t z_end = even_frame ? world->grid_z : -1;
    int32_t z_step = even_frame ? 1 : -1;
    
    for (int32_t y = 1; y < world->grid_y; y++) {
        for (int32_t z = z_start; z != z_end; z += z_step) {
            for (int32_t x = x_start; x != x_end; x += x_step) {
                int32_t idx = voxel_world_index(world, x, y, z);
                if (!world->voxels[idx].active) {
                    continue;
                }
                
                if (!voxel_world_get(world, x, y - 1, z).active) {
                    swap_voxels(world, x, y, z, x, y - 1, z);
                    continue;
                }
                
                int32_t dx_primary = even_frame ? -1 : 1;
                int32_t dz_primary = even_frame ? -1 : 1;
                
                bool moved = false;
                
                int32_t diagonals[4][2] = {
                    { dx_primary, 0 },
                    { 0, dz_primary },
                    { -dx_primary, 0 },
                    { 0, -dz_primary }
                };
                
                for (int32_t d = 0; d < 4 && !moved; d++) {
                    int32_t nx = x + diagonals[d][0];
                    int32_t nz = z + diagonals[d][1];
                    
                    if (voxel_world_in_bounds(world, nx, y - 1, nz) &&
                        !voxel_world_get(world, nx, y - 1, nz).active &&
                        !voxel_world_get(world, nx, y, nz).active) {
                        swap_voxels(world, x, y, z, nx, y - 1, nz);
                        moved = true;
                    }
                }
            }
        }
    }
}

void voxel_world_spawn_sphere(VoxelWorld* world, Vec3 center, float radius, Vec3 color) {
    uint8_t r = (uint8_t)(color.x * 255.0f);
    uint8_t g = (uint8_t)(color.y * 255.0f);
    uint8_t b = (uint8_t)(color.z * 255.0f);
    
    int32_t cx, cy, cz;
    voxel_world_to_grid(world, center, &cx, &cy, &cz);
    
    int32_t voxel_radius = (int32_t)(radius / world->voxel_size) + 1;
    
    for (int32_t dy = -voxel_radius; dy <= voxel_radius; dy++) {
        for (int32_t dz = -voxel_radius; dz <= voxel_radius; dz++) {
            for (int32_t dx = -voxel_radius; dx <= voxel_radius; dx++) {
                float dist = sqrtf((float)(dx*dx + dy*dy + dz*dz)) * world->voxel_size;
                if (dist <= radius) {
                    voxel_world_set(world, cx + dx, cy + dy, cz + dz, r, g, b);
                }
            }
        }
    }
}

void voxel_world_spawn_explosion(VoxelWorld* world, Vec3 center, float radius, int32_t count, Vec3 color) {
    uint8_t r = (uint8_t)(color.x * 255.0f);
    uint8_t g = (uint8_t)(color.y * 255.0f);
    uint8_t b = (uint8_t)(color.z * 255.0f);
    
    int32_t cx, cy, cz;
    voxel_world_to_grid(world, center, &cx, &cy, &cz);
    
    int32_t voxel_radius = (int32_t)(radius / world->voxel_size) + 1;
    
    int32_t spawned = 0;
    int32_t attempts = 0;
    int32_t max_attempts = count * 10;
    
    while (spawned < count && attempts < max_attempts) {
        int32_t dx = (rand() % (voxel_radius * 2 + 1)) - voxel_radius;
        int32_t dy = (rand() % (voxel_radius * 2 + 1)) - voxel_radius;
        int32_t dz = (rand() % (voxel_radius * 2 + 1)) - voxel_radius;
        
        float dist = sqrtf((float)(dx*dx + dy*dy + dz*dz)) * world->voxel_size;
        if (dist <= radius) {
            int32_t px = cx + dx;
            int32_t py = cy + dy;
            int32_t pz = cz + dz;
            
            if (voxel_world_in_bounds(world, px, py, pz) && 
                !voxel_world_get(world, px, py, pz).active) {
                voxel_world_set(world, px, py, pz, r, g, b);
                spawned++;
            }
        }
        attempts++;
    }
}
