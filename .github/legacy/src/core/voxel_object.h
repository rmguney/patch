#ifndef PATCH_CORE_VOXEL_OBJECT_H
#define PATCH_CORE_VOXEL_OBJECT_H

#include "types.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOBJ_GRID_SIZE 12
#define VOBJ_TOTAL_VOXELS (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE)
#define VOBJ_MAX_OBJECTS 256

typedef struct {
    uint8_t active;
    uint8_t r, g, b;
} VObjVoxel;

typedef struct {
    Vec3 position;
    Vec3 velocity;
    Vec3 angular_velocity;
    Vec3 rotation;
    
    VObjVoxel voxels[VOBJ_TOTAL_VOXELS];
    int32_t voxel_count;
    
    float voxel_size;
    float mass;
    float radius;
    Vec3 shape_center_offset;
    Vec3 shape_half_extents;
    Vec3 support_min;
    Vec3 support_max;
    
    Vec3 base_color;
    bool active;
} VoxelObject;

typedef struct {
    VoxelObject objects[VOBJ_MAX_OBJECTS];
    int32_t object_count;
    
    Bounds3D bounds;
    Vec3 gravity;
    
    Vec3 mouse_pos;
    Vec3 mouse_prev_pos;
    float mouse_radius;
    float mouse_strength;
    bool mouse_active;
    
    float damping;
    float restitution;
    float floor_friction;
} VoxelObjectWorld;

VoxelObjectWorld* voxel_object_world_create(Bounds3D bounds);
void voxel_object_world_destroy(VoxelObjectWorld* world);

int32_t voxel_object_world_add_sphere(VoxelObjectWorld* world, Vec3 position, float radius, Vec3 color);
int32_t voxel_object_world_add_box(VoxelObjectWorld* world, Vec3 position, Vec3 half_extents, Vec3 color);
int32_t voxel_object_world_add_cylinder(VoxelObjectWorld* world, Vec3 position, float radius, float height, Vec3 color);
int32_t voxel_object_world_add_torus(VoxelObjectWorld* world, Vec3 position, float major_radius, float tube_radius, Vec3 color);
int32_t voxel_object_world_add_tesseract(VoxelObjectWorld* world, Vec3 position, float outer_half_extent, float inner_half_extent, float thickness, Vec3 color);
int32_t voxel_object_world_add_crystal(VoxelObjectWorld* world, Vec3 position, float radius, float height, Vec3 color);
int32_t voxel_object_world_add_gyroid(VoxelObjectWorld* world, Vec3 position, float radius, float thickness, Vec3 color);
void voxel_object_world_remove(VoxelObjectWorld* world, int32_t index);

void voxel_object_world_update(VoxelObjectWorld* world, float dt);

void voxel_object_world_set_mouse(VoxelObjectWorld* world, Vec3 position, Vec3 prev_position, float radius, float strength, bool active);

typedef struct {
    int32_t object_index;
    Vec3 impact_point;
    Vec3 impact_normal;
    bool hit;
} VoxelObjectHit;

VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld* world, Vec3 ray_origin, Vec3 ray_dir);

int32_t voxel_object_destroy_at_point(VoxelObjectWorld* world, int32_t obj_index, 
                                       Vec3 impact_point, float destroy_radius, int32_t max_destroy,
                                       Vec3* out_positions, Vec3* out_colors, int32_t max_particles);

bool voxel_object_get_voxel_world_pos(const VoxelObject* obj, int32_t x, int32_t y, int32_t z, Vec3* out_pos);

static inline int32_t vobj_index(int32_t x, int32_t y, int32_t z) {
    return x + y * VOBJ_GRID_SIZE + z * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE;
}

static inline void vobj_coords(int32_t idx, int32_t* x, int32_t* y, int32_t* z) {
    *x = idx % VOBJ_GRID_SIZE;
    *y = (idx / VOBJ_GRID_SIZE) % VOBJ_GRID_SIZE;
    *z = idx / (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE);
}

#ifdef __cplusplus
}
#endif

#endif
