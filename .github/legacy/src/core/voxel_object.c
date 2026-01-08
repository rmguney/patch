#include "voxel_object.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int32_t allocate_object_slot(VoxelObjectWorld* world) {
    for (int32_t i = 0; i < world->object_count; i++) {
        if (!world->objects[i].active) {
            return i;
        }
    }
    if (world->object_count >= VOBJ_MAX_OBJECTS) return -1;
    return world->object_count++;
}

static inline void set_voxel_color(VObjVoxel* v, Vec3 color) {
    float variation = 0.9f + ((float)rand() / (float)RAND_MAX) * 0.2f;
    v->active = 1;
    v->r = (uint8_t)clampf(color.x * variation * 255.0f, 0.0f, 255.0f);
    v->g = (uint8_t)clampf(color.y * variation * 255.0f, 0.0f, 255.0f);
    v->b = (uint8_t)clampf(color.z * variation * 255.0f, 0.0f, 255.0f);
}

static float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static void flood_fill_voxels(const VoxelObject* obj, uint8_t* visited, int32_t x, int32_t y, int32_t z) {
    if (x < 0 || x >= VOBJ_GRID_SIZE || y < 0 || y >= VOBJ_GRID_SIZE || z < 0 || z >= VOBJ_GRID_SIZE) return;
    int32_t idx = vobj_index(x, y, z);
    if (visited[idx] || !obj->voxels[idx].active) return;
    
    visited[idx] = 1;
    flood_fill_voxels(obj, visited, x-1, y, z);
    flood_fill_voxels(obj, visited, x+1, y, z);
    flood_fill_voxels(obj, visited, x, y-1, z);
    flood_fill_voxels(obj, visited, x, y+1, z);
    flood_fill_voxels(obj, visited, x, y, z-1);
    flood_fill_voxels(obj, visited, x, y, z+1);
}

static void recalc_object_shape(VoxelObject* obj) {
    if (obj->voxel_count <= 0) {
        obj->active = false;
        return;
    }
    
    int32_t min_y = VOBJ_GRID_SIZE, max_y = 0;
    int32_t min_x = VOBJ_GRID_SIZE, max_x = 0;
    int32_t min_z = VOBJ_GRID_SIZE, max_z = 0;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                if (obj->voxels[vobj_index(x, y, z)].active) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                    if (z < min_z) min_z = z;
                    if (z > max_z) max_z = z;
                }
            }
        }
    }
    
    float extent_x = (float)(max_x - min_x + 1) * obj->voxel_size * 0.5f;
    float extent_y = (float)(max_y - min_y + 1) * obj->voxel_size * 0.5f;
    float extent_z = (float)(max_z - min_z + 1) * obj->voxel_size * 0.5f;
    obj->shape_half_extents = vec3_create(extent_x, extent_y, extent_z);
    obj->radius = sqrtf(extent_x*extent_x + extent_y*extent_y + extent_z*extent_z);
    obj->mass = (float)obj->voxel_count * 0.1f;

    float half_size_full = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    float center_x = ((float)min_x + (float)max_x + 1.0f) * 0.5f;
    float center_y = ((float)min_y + (float)max_y + 1.0f) * 0.5f;
    float center_z = ((float)min_z + (float)max_z + 1.0f) * 0.5f;
    obj->shape_center_offset = vec3_create(
        center_x * obj->voxel_size - half_size_full,
        center_y * obj->voxel_size - half_size_full,
        center_z * obj->voxel_size - half_size_full
    );
    
    int32_t support_min_x = VOBJ_GRID_SIZE, support_max_x = 0;
    int32_t support_min_z = VOBJ_GRID_SIZE, support_max_z = 0;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
            if (obj->voxels[vobj_index(x, min_y, z)].active) {
                if (x < support_min_x) support_min_x = x;
                if (x > support_max_x) support_max_x = x;
                if (z < support_min_z) support_min_z = z;
                if (z > support_max_z) support_max_z = z;
            }
        }
    }
    
    float support_cx = ((float)support_min_x + (float)support_max_x + 1.0f) * 0.5f * obj->voxel_size - half_size_full;
    float support_cz = ((float)support_min_z + (float)support_max_z + 1.0f) * 0.5f * obj->voxel_size - half_size_full;
    float support_half_x = ((float)(support_max_x - support_min_x + 1) * 0.5f) * obj->voxel_size;
    float support_half_z = ((float)(support_max_z - support_min_z + 1) * 0.5f) * obj->voxel_size;
    obj->support_min = vec3_create(support_cx - support_half_x, 0.0f, support_cz - support_half_z);
    obj->support_max = vec3_create(support_cx + support_half_x, 0.0f, support_cz + support_half_z);
}

static void split_disconnected_islands(VoxelObjectWorld* world, int32_t obj_index) {
    VoxelObject* obj = &world->objects[obj_index];
    if (!obj->active || obj->voxel_count <= 1) return;
    
    uint8_t visited[VOBJ_TOTAL_VOXELS] = {0};
    
    int32_t first_x = -1, first_y = -1, first_z = -1;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS && first_x < 0; i++) {
        if (obj->voxels[i].active) {
            vobj_coords(i, &first_x, &first_y, &first_z);
        }
    }
    if (first_x < 0) return;
    
    flood_fill_voxels(obj, visited, first_x, first_y, first_z);
    
    int32_t unvisited_count = 0;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++) {
        if (obj->voxels[i].active && !visited[i]) {
            unvisited_count++;
        }
    }
    if (unvisited_count == 0) return;
    
    if (world->object_count >= VOBJ_MAX_OBJECTS) return;
    
    VoxelObject* new_obj = &world->objects[world->object_count];
    memset(new_obj, 0, sizeof(VoxelObject));
    new_obj->position = obj->position;
    new_obj->velocity = obj->velocity;
    new_obj->angular_velocity = obj->angular_velocity;
    new_obj->rotation = obj->rotation;
    new_obj->voxel_size = obj->voxel_size;
    new_obj->base_color = obj->base_color;
    new_obj->active = true;
    new_obj->voxel_count = 0;
    
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++) {
        if (obj->voxels[i].active && !visited[i]) {
            new_obj->voxels[i] = obj->voxels[i];
            new_obj->voxel_count++;
            obj->voxels[i].active = 0;
            obj->voxel_count--;
        }
    }
    
    world->object_count++;
    
    recalc_object_shape(obj);
    recalc_object_shape(new_obj);
    
    split_disconnected_islands(world, obj_index);
    split_disconnected_islands(world, world->object_count - 1);
}

VoxelObjectWorld* voxel_object_world_create(Bounds3D bounds) {
    VoxelObjectWorld* world = (VoxelObjectWorld*)calloc(1, sizeof(VoxelObjectWorld));
    if (!world) return NULL;
    
    world->bounds = bounds;
    world->gravity = vec3_create(0.0f, -12.0f, 0.0f);
    world->damping = 0.995f;
    world->restitution = 0.4f;
    world->floor_friction = 0.85f;
    world->object_count = 0;
    
    world->mouse_pos = vec3_zero();
    world->mouse_prev_pos = vec3_zero();
    world->mouse_radius = 2.5f;
    world->mouse_strength = 15.0f;
    world->mouse_active = false;
    
    return world;
}

void voxel_object_world_destroy(VoxelObjectWorld* world) {
    if (world) {
        free(world);
    }
}

void voxel_object_world_remove(VoxelObjectWorld* world, int32_t index) {
    if (index < 0 || index >= world->object_count) return;
    world->objects[index].active = false;
}

int32_t voxel_object_world_add_sphere(VoxelObjectWorld* world, Vec3 position, float radius, Vec3 color) {
    for (int32_t i = 0; i < world->object_count; i++) {
        if (!world->objects[i].active) {
            VoxelObject* obj = &world->objects[i];
            memset(obj, 0, sizeof(VoxelObject));
            obj->position = position;
            obj->velocity = vec3_zero();
            obj->angular_velocity = vec3_create(
                ((float)rand() / RAND_MAX - 0.5f) * 0.5f,
                ((float)rand() / RAND_MAX - 0.5f) * 0.5f,
                ((float)rand() / RAND_MAX - 0.5f) * 0.5f
            );
            obj->rotation = vec3_zero();
            obj->radius = radius;
            obj->shape_center_offset = vec3_zero();
            obj->shape_half_extents = vec3_create(radius, radius, radius);
            goto fill_sphere;
        fill_sphere:;
            float voxel_size = (radius * 2.0f) / VOBJ_GRID_SIZE;
            obj->voxel_size = voxel_size;
            obj->voxel_count = 0;
            Vec3 center = vec3_create(VOBJ_GRID_SIZE * 0.5f, VOBJ_GRID_SIZE * 0.5f, VOBJ_GRID_SIZE * 0.5f);
            float grid_radius = radius / voxel_size;
            for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
                for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
                    for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                        float dx = x + 0.5f - center.x;
                        float dy = y + 0.5f - center.y;
                        float dz = z + 0.5f - center.z;
                        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                        int32_t idx = vobj_index(x, y, z);
                        if (dist <= grid_radius) {
                            obj->voxels[idx].active = 1;
                            obj->voxels[idx].r = (uint8_t)(color.x * 255);
                            obj->voxels[idx].g = (uint8_t)(color.y * 255);
                            obj->voxels[idx].b = (uint8_t)(color.z * 255);
                            obj->voxel_count++;
                        } else {
                            obj->voxels[idx].active = 0;
                        }
                    }
                }
            }
            obj->base_color = color;
            obj->active = true;
            obj->mass = obj->voxel_count * 0.1f;
            return i;
        }
    }
    if (world->object_count >= VOBJ_MAX_OBJECTS) return -1;
    
    VoxelObject* obj = &world->objects[world->object_count];
    memset(obj, 0, sizeof(VoxelObject));
    
    obj->position = position;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.5f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.5f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.5f
    );
    obj->rotation = vec3_zero();
    obj->radius = radius;
    obj->shape_center_offset = vec3_zero();
    obj->shape_half_extents = vec3_create(radius, radius, radius);
    obj->support_min = vec3_create(-radius, -radius, -radius);
    obj->support_max = vec3_create(radius, radius, radius);
    obj->base_color = color;
    obj->active = true;
    
    obj->voxel_size = (radius * 2.0f) / (float)VOBJ_GRID_SIZE;
    
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float r_voxels = radius / obj->voxel_size;
    
    obj->voxel_count = 0;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = (float)x - half_grid + 0.5f;
                float dy = (float)y - half_grid + 0.5f;
                float dz = (float)z - half_grid + 0.5f;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                
                int32_t idx = vobj_index(x, y, z);
                
                if (dist <= r_voxels) {
                    obj->voxels[idx].active = 1;
                    
                    float variation = 0.9f + ((float)rand() / (float)RAND_MAX) * 0.2f;
                    obj->voxels[idx].r = (uint8_t)clampf(color.x * variation * 255.0f, 0.0f, 255.0f);
                    obj->voxels[idx].g = (uint8_t)clampf(color.y * variation * 255.0f, 0.0f, 255.0f);
                    obj->voxels[idx].b = (uint8_t)clampf(color.z * variation * 255.0f, 0.0f, 255.0f);
                    
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }
    
    obj->mass = (float)obj->voxel_count * 0.1f;
    
    return world->object_count++;
}

int32_t voxel_object_world_add_box(VoxelObjectWorld* world, Vec3 position, Vec3 half_extents, Vec3 color) {
    int32_t slot = allocate_object_slot(world);
    if (slot < 0) return -1;

    VoxelObject* obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));
    
    obj->position = position;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.3f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.3f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.3f
    );
    obj->rotation = vec3_zero();
    obj->radius = vec3_length(half_extents);
    obj->shape_center_offset = vec3_zero();
    obj->shape_half_extents = half_extents;
    obj->support_min = vec3_scale(half_extents, -1.0f);
    obj->support_max = half_extents;
    obj->base_color = color;
    obj->active = true;
    
    float max_extent = fmaxf(half_extents.x, fmaxf(half_extents.y, half_extents.z));
    obj->voxel_size = (max_extent * 2.0f) / (float)VOBJ_GRID_SIZE;
    
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    
    obj->voxel_count = 0;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;
                
                int32_t idx = vobj_index(x, y, z);
                
                if (fabsf(dx) <= half_extents.x && fabsf(dy) <= half_extents.y && fabsf(dz) <= half_extents.z) {
                    set_voxel_color(&obj->voxels[idx], color);
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }
    
    obj->mass = (float)obj->voxel_count * 0.1f;
    return slot;
}

int32_t voxel_object_world_add_cylinder(VoxelObjectWorld* world, Vec3 position, float radius, float height, Vec3 color) {
    int32_t slot = allocate_object_slot(world);
    if (slot < 0) return -1;

    VoxelObject* obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));
    
    obj->position = position;
    obj->position.y += height * 0.5f;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.3f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.3f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.3f
    );
    obj->rotation = vec3_zero();
    obj->radius = sqrtf(radius * radius + height * height * 0.25f);
    obj->shape_center_offset = vec3_zero();
    obj->shape_half_extents = vec3_create(radius, height * 0.5f, radius);
    obj->support_min = vec3_create(-radius, -height * 0.5f, -radius);
    obj->support_max = vec3_create(radius, height * 0.5f, radius);
    obj->base_color = color;
    obj->active = true;
    
    float max_extent = fmaxf(radius, height * 0.5f);
    obj->voxel_size = (max_extent * 2.0f) / (float)VOBJ_GRID_SIZE;
    
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float r_voxels = radius / obj->voxel_size;
    float h_voxels = (height * 0.5f) / obj->voxel_size;
    
    obj->voxel_count = 0;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = (float)x - half_grid + 0.5f;
                float dy = (float)y - half_grid + 0.5f;
                float dz = (float)z - half_grid + 0.5f;
                
                float radial_dist = sqrtf(dx*dx + dz*dz);
                
                int32_t idx = vobj_index(x, y, z);
                
                if (radial_dist <= r_voxels && fabsf(dy) <= h_voxels) {
                    set_voxel_color(&obj->voxels[idx], color);
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }
    
    obj->mass = (float)obj->voxel_count * 0.1f;
    return slot;
}

int32_t voxel_object_world_add_torus(VoxelObjectWorld* world, Vec3 position, float major_radius, float tube_radius, Vec3 color) {
    int32_t slot = allocate_object_slot(world);
    if (slot < 0) return -1;

    VoxelObject* obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.4f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.4f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.4f
    );
    obj->rotation = vec3_zero();
    obj->base_color = color;
    obj->active = true;

    float max_extent = major_radius + tube_radius;
    obj->voxel_size = (max_extent * 2.0f) / (float)VOBJ_GRID_SIZE;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float mr2 = major_radius * major_radius;
    float tr2 = tube_radius * tube_radius;

    obj->voxel_count = 0;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;

                float r = sqrtf(dx * dx + dz * dz);
                float q = r - major_radius;
                float d2 = q * q + dy * dy;

                int32_t idx = vobj_index(x, y, z);
                if (d2 <= tr2 && (dx * dx + dz * dz) <= (max_extent * max_extent + tr2 + mr2)) {
                    set_voxel_color(&obj->voxels[idx], color);
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }

    recalc_object_shape(obj);
    return slot;
}

static float dist_point_segment_sq(Vec3 p, Vec3 a, Vec3 b) {
    Vec3 ab = vec3_sub(b, a);
    Vec3 ap = vec3_sub(p, a);
    float ab_len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (ab_len2 <= 1e-8f) {
        float dx = ap.x;
        float dy = ap.y;
        float dz = ap.z;
        return dx * dx + dy * dy + dz * dz;
    }
    float t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / ab_len2;
    t = clamp01(t);
    Vec3 q = vec3_add(a, vec3_scale(ab, t));
    Vec3 d = vec3_sub(p, q);
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

int32_t voxel_object_world_add_tesseract(VoxelObjectWorld* world, Vec3 position, float outer_half_extent, float inner_half_extent, float thickness, Vec3 color) {
    int32_t slot = allocate_object_slot(world);
    if (slot < 0) return -1;

    VoxelObject* obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.25f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.55f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.25f
    );
    obj->rotation = vec3_zero();
    obj->base_color = color;
    obj->active = true;

    float max_extent = outer_half_extent;
    obj->voxel_size = (max_extent * 2.0f) / (float)VOBJ_GRID_SIZE;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float t = fmaxf(thickness, obj->voxel_size * 1.25f);
    float t2 = t * t;

    obj->voxel_count = 0;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;

                float ax = fabsf(dx);
                float ay = fabsf(dy);
                float az = fabsf(dz);

                bool in_outer = (ax <= outer_half_extent && ay <= outer_half_extent && az <= outer_half_extent);
                bool in_inner = (ax <= inner_half_extent && ay <= inner_half_extent && az <= inner_half_extent);

                int near_outer = (ax >= outer_half_extent - t) + (ay >= outer_half_extent - t) + (az >= outer_half_extent - t);
                int near_inner = (ax >= inner_half_extent - t) + (ay >= inner_half_extent - t) + (az >= inner_half_extent - t);

                bool frame = (in_outer && near_outer >= 2) || (in_inner && near_inner >= 2);

                Vec3 p = vec3_create(dx, dy, dz);
                bool connector = false;
                if (!frame) {
                    for (int sx = -1; sx <= 1 && !connector; sx += 2) {
                        for (int sy = -1; sy <= 1 && !connector; sy += 2) {
                            for (int sz = -1; sz <= 1 && !connector; sz += 2) {
                                Vec3 a = vec3_create((float)sx * inner_half_extent, (float)sy * inner_half_extent, (float)sz * inner_half_extent);
                                Vec3 b = vec3_create((float)sx * outer_half_extent, (float)sy * outer_half_extent, (float)sz * outer_half_extent);
                                if (dist_point_segment_sq(p, a, b) <= t2) {
                                    connector = true;
                                }
                            }
                        }
                    }
                }

                int32_t idx = vobj_index(x, y, z);
                if (frame || connector) {
                    set_voxel_color(&obj->voxels[idx], color);
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }

    recalc_object_shape(obj);
    return slot;
}

int32_t voxel_object_world_add_crystal(VoxelObjectWorld* world, Vec3 position, float radius, float height, Vec3 color) {
    int32_t slot = allocate_object_slot(world);
    if (slot < 0) return -1;

    VoxelObject* obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->position.y += height * 0.5f;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.2f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.6f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.2f
    );
    obj->rotation = vec3_zero();
    obj->base_color = color;
    obj->active = true;

    float half_h = height * 0.5f;
    float max_extent = fmaxf(radius, half_h);
    obj->voxel_size = (max_extent * 2.0f) / (float)VOBJ_GRID_SIZE;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    obj->voxel_count = 0;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;

                float nx = fabsf(dx) / radius;
                float ny = fabsf(dy) / half_h;
                float nz = fabsf(dz) / radius;

                float d = nx + ny + nz;

                int32_t idx = vobj_index(x, y, z);
                if (d <= 1.0f) {
                    set_voxel_color(&obj->voxels[idx], color);
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }

    recalc_object_shape(obj);
    return slot;
}

int32_t voxel_object_world_add_gyroid(VoxelObjectWorld* world, Vec3 position, float radius, float thickness, Vec3 color) {
    int32_t slot = allocate_object_slot(world);
    if (slot < 0) return -1;

    VoxelObject* obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->velocity = vec3_zero();
    obj->angular_velocity = vec3_create(
        ((float)rand() / RAND_MAX - 0.5f) * 0.35f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.35f,
        ((float)rand() / RAND_MAX - 0.5f) * 0.35f
    );
    obj->rotation = vec3_zero();
    obj->base_color = color;
    obj->active = true;

    obj->voxel_size = (radius * 2.0f) / (float)VOBJ_GRID_SIZE;
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float r2 = radius * radius;

    float freq = 2.4f;
    float thick = fmaxf(thickness, obj->voxel_size * 0.85f);

    obj->voxel_count = 0;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;

                float d2 = dx * dx + dy * dy + dz * dz;
                int32_t idx = vobj_index(x, y, z);
                if (d2 > r2) {
                    obj->voxels[idx].active = 0;
                    continue;
                }

                float px = dx / radius;
                float py = dy / radius;
                float pz = dz / radius;
                float f = sinf(px * 3.14159265f * freq) * cosf(py * 3.14159265f * freq)
                        + sinf(py * 3.14159265f * freq) * cosf(pz * 3.14159265f * freq)
                        + sinf(pz * 3.14159265f * freq) * cosf(px * 3.14159265f * freq);

                if (fabsf(f) <= (thick / radius) * 1.15f) {
                    set_voxel_color(&obj->voxels[idx], color);
                    obj->voxel_count++;
                } else {
                    obj->voxels[idx].active = 0;
                }
            }
        }
    }

    recalc_object_shape(obj);
    return slot;
}

bool voxel_object_get_voxel_world_pos(const VoxelObject* obj, int32_t x, int32_t y, int32_t z, Vec3* out_pos) {
    if (x < 0 || x >= VOBJ_GRID_SIZE || y < 0 || y >= VOBJ_GRID_SIZE || z < 0 || z >= VOBJ_GRID_SIZE) {
        return false;
    }
    
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    
    out_pos->x = obj->position.x + ((float)x + 0.5f) * obj->voxel_size - half_size;
    out_pos->y = obj->position.y + ((float)y + 0.5f) * obj->voxel_size - half_size;
    out_pos->z = obj->position.z + ((float)z + 0.5f) * obj->voxel_size - half_size;
    
    return true;
}

static void apply_topple_torque(VoxelObject* obj, const Bounds3D* bounds, float dt) {
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    Vec3 pivot = vec3_add(obj->position, obj->shape_center_offset);
    Mat4 rotation = mat4_rotation_euler(obj->rotation);
    
    float lowest_y = 1e10f;
    Vec3 lowest_voxel_world = vec3_zero();
    Vec3 com_world = pivot;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                if (!obj->voxels[vobj_index(x, y, z)].active) continue;
                
                Vec3 local;
                local.x = ((float)x + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.x;
                local.y = ((float)y + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.y;
                local.z = ((float)z + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.z;
                
                Vec3 world = vec3_add(pivot, mat4_transform_point(rotation, local));
                float vhalf = obj->voxel_size * 0.5f;
                
                if (world.y - vhalf < lowest_y) {
                    lowest_y = world.y - vhalf;
                    lowest_voxel_world = world;
                }
            }
        }
    }
    
    float floor_dist = lowest_y - bounds->min_y;
    if (floor_dist > 0.05f) return;
    
    Vec3 contact_to_com = vec3_sub(com_world, lowest_voxel_world);
    
    float torque_strength = 25.0f;
    obj->angular_velocity.z -= contact_to_com.x * torque_strength * dt;
    obj->angular_velocity.x += contact_to_com.z * torque_strength * dt;
}

static void resolve_rotated_ground_collision(VoxelObject* obj, const Bounds3D* bounds, float restitution, float friction) {
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    Vec3 pivot = vec3_add(obj->position, obj->shape_center_offset);
    Mat4 rotation = mat4_rotation_euler(obj->rotation);
    
    float lowest_y = 1e10f;
    float highest_y = -1e10f;
    float leftmost_x = 1e10f;
    float rightmost_x = -1e10f;
    float nearest_z = 1e10f;
    float farthest_z = -1e10f;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++) {
                if (!obj->voxels[vobj_index(x, y, z)].active) continue;
                
                Vec3 local;
                local.x = ((float)x + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.x;
                local.y = ((float)y + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.y;
                local.z = ((float)z + 0.5f) * obj->voxel_size - half_size - obj->shape_center_offset.z;
                
                Vec3 world = vec3_add(pivot, mat4_transform_point(rotation, local));
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
    
    if (lowest_y < bounds->min_y) {
        float penetration = bounds->min_y - lowest_y;
        obj->position.y += penetration;
        obj->velocity.y = -obj->velocity.y * restitution;
        obj->angular_velocity.x += obj->velocity.z * 0.3f;
        obj->angular_velocity.z -= obj->velocity.x * 0.3f;
        obj->velocity.x *= friction;
        obj->velocity.z *= friction;
        obj->angular_velocity = vec3_scale(obj->angular_velocity, friction);
    }
    if (highest_y > bounds->max_y) {
        float penetration = highest_y - bounds->max_y;
        obj->position.y -= penetration;
        obj->velocity.y = -obj->velocity.y * restitution;
    }
    if (leftmost_x < bounds->min_x) {
        float penetration = bounds->min_x - leftmost_x;
        obj->position.x += penetration;
        obj->velocity.x = -obj->velocity.x * restitution;
        obj->angular_velocity.z += obj->velocity.y * 0.3f;
        obj->angular_velocity.y -= obj->velocity.z * 0.1f;
        obj->angular_velocity = vec3_scale(obj->angular_velocity, friction);
    }
    if (rightmost_x > bounds->max_x) {
        float penetration = rightmost_x - bounds->max_x;
        obj->position.x -= penetration;
        obj->velocity.x = -obj->velocity.x * restitution;
        obj->angular_velocity.z -= obj->velocity.y * 0.3f;
        obj->angular_velocity.y += obj->velocity.z * 0.1f;
        obj->angular_velocity = vec3_scale(obj->angular_velocity, friction);
    }
    if (nearest_z < bounds->min_z) {
        float penetration = bounds->min_z - nearest_z;
        obj->position.z += penetration;
        obj->velocity.z = -obj->velocity.z * restitution;
        obj->angular_velocity.x -= obj->velocity.y * 0.3f;
        obj->angular_velocity.y += obj->velocity.x * 0.1f;
        obj->angular_velocity = vec3_scale(obj->angular_velocity, friction);
    }
    if (farthest_z > bounds->max_z) {
        float penetration = farthest_z - bounds->max_z;
        obj->position.z -= penetration;
        obj->velocity.z = -obj->velocity.z * restitution;
        obj->angular_velocity.x += obj->velocity.y * 0.3f;
        obj->angular_velocity.y -= obj->velocity.x * 0.1f;
        obj->angular_velocity = vec3_scale(obj->angular_velocity, friction);
    }
}

static void resolve_object_collision(VoxelObject* a, VoxelObject* b, float restitution) {
    Vec3 a_center = vec3_add(a->position, a->shape_center_offset);
    Vec3 b_center = vec3_add(b->position, b->shape_center_offset);
    Vec3 delta = vec3_sub(b_center, a_center);
    float dist = vec3_length(delta);
    float min_dist = a->radius + b->radius;
    
    if (dist >= min_dist || dist < 0.0001f) return;
    
    Vec3 normal = vec3_scale(delta, 1.0f / dist);
    float overlap = min_dist - dist;
    
    float total_mass = a->mass + b->mass;
    float a_ratio = b->mass / total_mass;
    float b_ratio = a->mass / total_mass;
    
    a_center = vec3_sub(a_center, vec3_scale(normal, overlap * a_ratio));
    b_center = vec3_add(b_center, vec3_scale(normal, overlap * b_ratio));
    a->position = vec3_sub(a_center, a->shape_center_offset);
    b->position = vec3_sub(b_center, b->shape_center_offset);
    
    Vec3 rel_vel = vec3_sub(a->velocity, b->velocity);
    float vel_along_normal = vec3_dot(rel_vel, normal);
    
    if (vel_along_normal > 0.0f) return;
    
    float j = -(1.0f + restitution) * vel_along_normal / total_mass;
    Vec3 impulse = vec3_scale(normal, j);
    
    a->velocity = vec3_add(a->velocity, vec3_scale(impulse, b->mass));
    b->velocity = vec3_sub(b->velocity, vec3_scale(impulse, a->mass));
    
    Vec3 tangent_vel = vec3_sub(rel_vel, vec3_scale(normal, vel_along_normal));
    float tangent_speed = vec3_length(tangent_vel);
    if (tangent_speed > 0.01f) {
        Vec3 tangent = vec3_scale(tangent_vel, 1.0f / tangent_speed);
        float friction_j = tangent_speed * 0.15f / total_mass;
        a->angular_velocity = vec3_add(a->angular_velocity, vec3_scale(vec3_cross(normal, tangent), friction_j * b->mass));
        b->angular_velocity = vec3_sub(b->angular_velocity, vec3_scale(vec3_cross(normal, tangent), friction_j * a->mass));
    }
}

void voxel_object_world_update(VoxelObjectWorld* world, float dt) {
    Vec3 mouse_velocity = vec3_zero();
    if (world->mouse_active) {
        mouse_velocity = vec3_scale(vec3_sub(world->mouse_pos, world->mouse_prev_pos), 1.0f / fmaxf(dt, 0.001f));
    }
    
    for (int32_t i = 0; i < world->object_count; i++) {
        VoxelObject* obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0) continue;
        
        obj->velocity = vec3_add(obj->velocity, vec3_scale(world->gravity, dt));
        
        apply_topple_torque(obj, &world->bounds, dt);
        
        if (world->mouse_active) {
            Vec3 center = vec3_add(obj->position, obj->shape_center_offset);
            Vec3 to_obj = vec3_sub(center, world->mouse_pos);
            float dist = vec3_length(to_obj);
            
            if (dist < world->mouse_radius && dist > 0.01f) {
                Vec3 push_dir = vec3_scale(to_obj, 1.0f / dist);
                
                float mouse_speed = vec3_length(mouse_velocity);
                float falloff = 1.0f - (dist / world->mouse_radius);
                falloff = falloff * falloff;
                
                Vec3 mouse_dir = mouse_speed > 0.05f ? vec3_scale(mouse_velocity, 1.0f / mouse_speed) : vec3_zero();

                float repel = (world->mouse_strength * 0.45f) * falloff;
                float brush = (mouse_speed * 0.90f) * falloff;

                Vec3 force = vec3_add(
                    vec3_scale(push_dir, repel * dt),
                    vec3_scale(mouse_dir, brush * dt)
                );

                obj->velocity = vec3_add(obj->velocity, force);
                
                Vec3 push_point = vec3_add(center, vec3_scale(vec3_negate(push_dir), obj->radius * 0.8f));
                Vec3 r = vec3_sub(push_point, center);
                Vec3 torque = vec3_cross(r, force);
                obj->angular_velocity = vec3_add(obj->angular_velocity, vec3_scale(torque, 0.5f));
            }
        }
        
        obj->velocity = vec3_scale(obj->velocity, world->damping);
        obj->angular_velocity = vec3_scale(obj->angular_velocity, 0.96f);
        obj->position = vec3_add(obj->position, vec3_scale(obj->velocity, dt));
        obj->rotation = vec3_add(obj->rotation, vec3_scale(obj->angular_velocity, dt));
        
        resolve_rotated_ground_collision(obj, &world->bounds, world->restitution, world->floor_friction);
    }
    
    for (int32_t i = 0; i < world->object_count; i++) {
        if (!world->objects[i].active) continue;
        
        for (int32_t j = i + 1; j < world->object_count; j++) {
            if (!world->objects[j].active) continue;
            
            resolve_object_collision(&world->objects[i], &world->objects[j], world->restitution);
        }
    }
}

void voxel_object_world_set_mouse(VoxelObjectWorld* world, Vec3 position, Vec3 prev_position, float radius, float strength, bool active) {
    world->mouse_pos = position;
    world->mouse_prev_pos = prev_position;
    world->mouse_radius = radius;
    world->mouse_strength = strength;
    world->mouse_active = active;
}

VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld* world, Vec3 ray_origin, Vec3 ray_dir) {
    VoxelObjectHit result;
    result.hit = false;
    result.object_index = -1;
    
    float closest_t = 1e30f;
    
    for (int32_t i = 0; i < world->object_count; i++) {
        VoxelObject* obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0) continue;

        Vec3 center = vec3_add(obj->position, obj->shape_center_offset);
        Vec3 oc = vec3_sub(ray_origin, center);
        float a = vec3_dot(ray_dir, ray_dir);
        float b = 2.0f * vec3_dot(oc, ray_dir);
        float c = vec3_dot(oc, oc) - obj->radius * obj->radius;
        float discriminant = b * b - 4.0f * a * c;
        
        if (discriminant < 0.0f) continue;
        
        float t_sphere = (-b - sqrtf(discriminant)) / (2.0f * a);
        if (t_sphere < 0.0f || t_sphere >= closest_t) continue;
        
        float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
        Vec3 local_origin = vec3_sub(ray_origin, obj->position);
        local_origin = vec3_add(local_origin, vec3_create(half_size, half_size, half_size));
        
        Vec3 inv_dir;
        inv_dir.x = (fabsf(ray_dir.x) > 0.0001f) ? 1.0f / ray_dir.x : 1e10f;
        inv_dir.y = (fabsf(ray_dir.y) > 0.0001f) ? 1.0f / ray_dir.y : 1e10f;
        inv_dir.z = (fabsf(ray_dir.z) > 0.0001f) ? 1.0f / ray_dir.z : 1e10f;
        
        float t_start = fmaxf(t_sphere - obj->radius * 0.1f, 0.0f);
        Vec3 pos = vec3_add(local_origin, vec3_scale(ray_dir, t_start));
        
        int32_t map_x = (int32_t)floorf(pos.x / obj->voxel_size);
        int32_t map_y = (int32_t)floorf(pos.y / obj->voxel_size);
        int32_t map_z = (int32_t)floorf(pos.z / obj->voxel_size);
        
        int32_t step_x = (ray_dir.x >= 0.0f) ? 1 : -1;
        int32_t step_y = (ray_dir.y >= 0.0f) ? 1 : -1;
        int32_t step_z = (ray_dir.z >= 0.0f) ? 1 : -1;
        
        float t_max_x = ((float)(map_x + (step_x > 0 ? 1 : 0)) * obj->voxel_size - pos.x) * inv_dir.x;
        float t_max_y = ((float)(map_y + (step_y > 0 ? 1 : 0)) * obj->voxel_size - pos.y) * inv_dir.y;
        float t_max_z = ((float)(map_z + (step_z > 0 ? 1 : 0)) * obj->voxel_size - pos.z) * inv_dir.z;
        
        float t_delta_x = fabsf(obj->voxel_size * inv_dir.x);
        float t_delta_y = fabsf(obj->voxel_size * inv_dir.y);
        float t_delta_z = fabsf(obj->voxel_size * inv_dir.z);
        
        float t_current = t_start;
        Vec3 hit_normal = vec3_create(0.0f, 0.0f, 0.0f);
        
        for (int32_t step = 0; step < VOBJ_GRID_SIZE * 3; step++) {
            if (map_x >= 0 && map_x < VOBJ_GRID_SIZE &&
                map_y >= 0 && map_y < VOBJ_GRID_SIZE &&
                map_z >= 0 && map_z < VOBJ_GRID_SIZE) {
                
                int32_t idx = vobj_index(map_x, map_y, map_z);
                if (obj->voxels[idx].active) {
                    if (t_current < closest_t) {
                        closest_t = t_current;
                        result.hit = true;
                        result.object_index = i;
                        result.impact_point = vec3_add(ray_origin, vec3_scale(ray_dir, t_current));
                        result.impact_normal = hit_normal;
                    }
                    break;
                }
            }
            
            if (t_max_x < t_max_y && t_max_x < t_max_z) {
                t_current = t_start + t_max_x;
                t_max_x += t_delta_x;
                map_x += step_x;
                hit_normal = vec3_create((float)(-step_x), 0.0f, 0.0f);
            } else if (t_max_y < t_max_z) {
                t_current = t_start + t_max_y;
                t_max_y += t_delta_y;
                map_y += step_y;
                hit_normal = vec3_create(0.0f, (float)(-step_y), 0.0f);
            } else {
                t_current = t_start + t_max_z;
                t_max_z += t_delta_z;
                map_z += step_z;
                hit_normal = vec3_create(0.0f, 0.0f, (float)(-step_z));
            }
            
            if (t_current > closest_t) break;
        }
    }
    
    return result;
}

int32_t voxel_object_destroy_at_point(VoxelObjectWorld* world, int32_t obj_index, 
                                       Vec3 impact_point, float destroy_radius, int32_t max_destroy,
                                       Vec3* out_positions, Vec3* out_colors, int32_t max_particles) {
    if (obj_index < 0 || obj_index >= world->object_count) return 0;
    
    VoxelObject* obj = &world->objects[obj_index];
    if (!obj->active) return 0;
    
    int32_t destroyed_count = 0;
    int32_t limit = max_destroy > 0 ? (max_destroy < max_particles ? max_destroy : max_particles) : max_particles;
    
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    
    for (int32_t z = 0; z < VOBJ_GRID_SIZE && destroyed_count < limit; z++) {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE && destroyed_count < limit; y++) {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE && destroyed_count < limit; x++) {
                int32_t idx = vobj_index(x, y, z);
                if (!obj->voxels[idx].active) continue;
                
                Vec3 voxel_pos;
                voxel_pos.x = obj->position.x + ((float)x + 0.5f) * obj->voxel_size - half_size;
                voxel_pos.y = obj->position.y + ((float)y + 0.5f) * obj->voxel_size - half_size;
                voxel_pos.z = obj->position.z + ((float)z + 0.5f) * obj->voxel_size - half_size;
                
                float dist = vec3_length(vec3_sub(voxel_pos, impact_point));
                
                if (dist < destroy_radius) {
                    out_positions[destroyed_count] = voxel_pos;
                    out_colors[destroyed_count] = vec3_create(
                        (float)obj->voxels[idx].r / 255.0f,
                        (float)obj->voxels[idx].g / 255.0f,
                        (float)obj->voxels[idx].b / 255.0f
                    );
                    
                    obj->voxels[idx].active = 0;
                    obj->voxel_count--;
                    destroyed_count++;
                }
            }
        }
    }
    
    if (obj->voxel_count <= 0) {
        obj->active = false;
    } else {
        recalc_object_shape(obj);
        split_disconnected_islands(world, obj_index);
    }
    
    return destroyed_count;
}
