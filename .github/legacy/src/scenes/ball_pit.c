#include "ball_pit.h"
#include <stdlib.h>
#include <string.h>

static float random_float(float min_val, float max_val) {
    return min_val + (float)rand() / (float)RAND_MAX * (max_val - min_val);
}

static Vec3 palette[] = {
    { 0.95f, 0.55f, 0.65f },
    { 0.55f, 0.85f, 0.85f },
    { 0.98f, 0.85f, 0.75f },
    { 0.70f, 0.90f, 0.80f },
    { 0.95f, 0.75f, 0.80f },
    { 0.75f, 0.80f, 0.95f },
    { 0.60f, 0.80f, 0.80f },
    { 0.90f, 0.70f, 0.75f },
    { 0.85f, 0.90f, 0.95f },
    { 0.95f, 0.80f, 0.85f },
};

static const int32_t PALETTE_COUNT = sizeof(palette) / sizeof(palette[0]);

static void ball_pit_init(Scene* scene) {
    BallPitData* data = (BallPitData*)scene->user_data;
    
    int32_t obj_target = 25;
    for (int32_t i = 0; i < obj_target; i++) {
        float x = random_float(scene->bounds.min_x * 0.7f, scene->bounds.max_x * 0.7f);
        float y = random_float(0.5f, scene->bounds.max_y * 0.8f);
        float z = random_float(scene->bounds.min_z * 0.7f, scene->bounds.max_z * 0.7f);
        float radius = random_float(0.35f, 0.55f);
        
        Vec3 color = palette[rand() % PALETTE_COUNT];
        int32_t idx = voxel_object_world_add_sphere(data->vobj_world, vec3_create(x, y, z), radius, color);
        if (idx >= 0) {
            data->vobj_world->objects[idx].velocity = vec3_create(
                random_float(-0.1f, 0.1f),
                0.0f,
                random_float(-0.1f, 0.1f)
            );
        }
    }
}

static void ball_pit_destroy_impl(Scene* scene) {
    BallPitData* data = (BallPitData*)scene->user_data;
    
    if (data->voxels) {
        voxel_world_destroy(data->voxels);
    }
    if (data->particles) {
        particle_system_destroy(data->particles);
    }
    if (data->vobj_world) {
        voxel_object_world_destroy(data->vobj_world);
    }
    
    free(data);
    free(scene);
}

static void ball_pit_update(Scene* scene, float dt) {
    BallPitData* data = (BallPitData*)scene->user_data;
    
    if (data->fragment_cooldown > 0.0f) {
        data->fragment_cooldown -= dt;
    }
    
    voxel_object_world_update(data->vobj_world, dt);
    particle_system_update(data->particles, dt);
    
    for (int32_t substep = 0; substep < data->voxel_physics_substeps; substep++) {
        voxel_world_update(data->voxels);
    }

    int32_t active_objects = 0;
    for (int32_t i = 0; i < data->vobj_world->object_count; i++) {
        if (data->vobj_world->objects[i].active) {
            active_objects++;
        }
    }

    if (active_objects == 0) {
        int32_t obj_target = 10;
        for (int32_t i = 0; i < obj_target && data->vobj_world->object_count < VOBJ_MAX_OBJECTS; i++) {
            float x = random_float(scene->bounds.min_x * 0.7f, scene->bounds.max_x * 0.7f);
            float y = random_float(0.5f, scene->bounds.max_y * 0.8f);
            float z = random_float(scene->bounds.min_z * 0.7f, scene->bounds.max_z * 0.7f);
            float radius = random_float(0.35f, 0.55f);
            Vec3 color = palette[rand() % PALETTE_COUNT];
            voxel_object_world_add_sphere(data->vobj_world, vec3_create(x, y, z), radius, color);
        }
    }
}

static void ball_pit_handle_input(Scene* scene, float mouse_x, float mouse_y, bool left_down, bool right_down) {
    BallPitData* data = (BallPitData*)scene->user_data;
    (void)mouse_x;
    (void)mouse_y;
    (void)right_down;
    
    if (left_down && data->fragment_cooldown <= 0.0f) {
        VoxelObjectHit hit_result = voxel_object_world_raycast(data->vobj_world, data->ray_origin, data->ray_dir);
        
        if (hit_result.hit) {
            Vec3 destroyed_positions[256];
            Vec3 destroyed_colors[256];
            
            float destroy_radius = 0.25f;
            
            int32_t destroyed = voxel_object_destroy_at_point(
                data->vobj_world, hit_result.object_index,
                hit_result.impact_point, destroy_radius, 0,
                destroyed_positions, destroyed_colors, 256
            );
            
            for (int32_t i = 0; i < destroyed; i++) {
                Vec3 dir = vec3_sub(destroyed_positions[i], hit_result.impact_point);
                float dist = vec3_length(dir);
                if (dist > 0.001f) {
                    dir = vec3_scale(dir, 1.0f / dist);
                } else {
                    dir = hit_result.impact_normal;
                }
                
                float speed = 3.0f + random_float(0.0f, 4.0f);
                dir = vec3_add(dir, vec3_scale(hit_result.impact_normal, 0.5f));
                dir = vec3_normalize(dir);
                
                Vec3 vel = vec3_scale(dir, speed);
                
                Particle* p = particle_system_add_slot(data->particles);
                p->position = destroyed_positions[i];
                p->velocity = vel;
                p->color = destroyed_colors[i];
                p->radius = 0.04f + random_float(0.0f, 0.02f);
                p->lifetime = 0.0f;
                p->active = true;
                p->settled = false;
            }
            
            data->fragment_cooldown = 0.08f;
        }
    }
}

static const char* ball_pit_get_name(Scene* scene) {
    (void)scene;
    return "Ball Pit";
}

static const SceneVTable ball_pit_vtable = {
    .init = ball_pit_init,
    .destroy = ball_pit_destroy_impl,
    .update = ball_pit_update,
    .handle_input = ball_pit_handle_input,
    .render = NULL,
    .get_name = ball_pit_get_name
};

Scene* ball_pit_scene_create(Bounds3D bounds) {
    Scene* scene = (Scene*)calloc(1, sizeof(Scene));
    if (!scene) return NULL;
    
    BallPitData* data = (BallPitData*)calloc(1, sizeof(BallPitData));
    if (!data) {
        free(scene);
        return NULL;
    }
    
    data->vobj_world = voxel_object_world_create(bounds);
    if (!data->vobj_world) {
        free(data);
        free(scene);
        return NULL;
    }
    
    data->particles = particle_system_create(bounds);
    if (!data->particles) {
        voxel_object_world_destroy(data->vobj_world);
        free(data);
        free(scene);
        return NULL;
    }
    
    data->voxels = voxel_world_create(bounds);
    if (!data->voxels) {
        particle_system_destroy(data->particles);
        voxel_object_world_destroy(data->vobj_world);
        free(data);
        free(scene);
        return NULL;
    }
    
    data->has_prev_mouse = false;
    data->fragment_cooldown = 0.0f;
    data->voxel_physics_substeps = 3;
    data->ray_origin = vec3_zero();
    data->ray_dir = vec3_create(0.0f, 0.0f, -1.0f);
    
    scene->vtable = &ball_pit_vtable;
    scene->bounds = bounds;
    scene->user_data = data;
    
    return scene;
}

void ball_pit_scene_destroy(Scene* scene) {
    scene_destroy(scene);
}

void ball_pit_set_ray(Scene* scene, Vec3 origin, Vec3 dir) {
    if (!scene || !scene->user_data) return;
    BallPitData* data = (BallPitData*)scene->user_data;
    data->ray_origin = origin;
    data->ray_dir = dir;
}

void ball_pit_set_mouse_world(Scene* scene, Vec3 world_pos, bool valid) {
    if (!scene || !scene->user_data) return;
    BallPitData* data = (BallPitData*)scene->user_data;
    
    if (valid && data->has_prev_mouse) {
        voxel_object_world_set_mouse(data->vobj_world, world_pos, data->prev_mouse_world, 2.25f, 20.0f, true);
    }
    
    data->prev_mouse_world = world_pos;
    data->has_prev_mouse = valid;
}
