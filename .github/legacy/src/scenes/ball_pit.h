#ifndef PATCH_SCENES_BALL_PIT_H
#define PATCH_SCENES_BALL_PIT_H

#include "../core/scene.h"
#include "../core/voxel_object.h"
#include "../core/particles.h"
#include "../core/voxel_physics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VoxelObjectWorld* vobj_world;
    ParticleSystem* particles;
    VoxelWorld* voxels;
    
    Vec3 prev_mouse_world;
    bool has_prev_mouse;
    float fragment_cooldown;
    int32_t voxel_physics_substeps;
    
    Vec3 ray_origin;
    Vec3 ray_dir;
} BallPitData;

Scene* ball_pit_scene_create(Bounds3D bounds);
void ball_pit_scene_destroy(Scene* scene);

void ball_pit_set_ray(Scene* scene, Vec3 origin, Vec3 dir);
void ball_pit_set_mouse_world(Scene* scene, Vec3 world_pos, bool valid);

#ifdef __cplusplus
}
#endif

#endif
