#ifndef PATCH_GAME_ROAM_H
#define PATCH_GAME_ROAM_H

#include "engine/sim/scene.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include "engine/physics/rigidbody.h"
#include "engine/physics/character.h"
#include "engine/voxel/connectivity.h"
#include "engine/core/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int32_t num_pillars;
        float terrain_amplitude;
        float terrain_frequency;
    } RoamParams;

    typedef struct
    {
        int32_t terrain_voxels;
        int32_t pillar_count;
        int32_t particles_active;
    } RoamStats;

    typedef struct
    {
        VoxelVolume *terrain;
        VoxelObjectWorld *objects;
        ParticleSystem *particles;
        PhysicsWorld *physics;
        ConnectivityWorkBuffer detach_work;
        Character player;

        RoamParams params;
        RoamStats stats;

        Vec3 ray_origin;
        Vec3 ray_dir;
        float voxel_size;

        float camera_yaw;
        float camera_pitch;
        Vec3 camera_offset;
        Vec3 camera_position;

        Vec3 move_input;
        bool jump_pressed;

        bool use_ortho_camera;
        bool left_was_down;
    } RoamData;

    RoamParams roam_default_params(void);
    Scene *roam_scene_create(Bounds3D bounds, float voxel_size, const RoamParams *params);
    void roam_scene_destroy(Scene *scene);

    void roam_set_ray(Scene *scene, Vec3 origin, Vec3 direction);
    bool roam_get_ortho_camera(Scene *scene);
    void roam_set_ortho_camera(Scene *scene, bool ortho);
    void roam_toggle_camera(Scene *scene);

    VoxelVolume *roam_get_terrain(Scene *scene);
    VoxelObjectWorld *roam_get_objects(Scene *scene);
    ParticleSystem *roam_get_particles(Scene *scene);
    PhysicsWorld *roam_get_physics(Scene *scene);
    const RoamStats *roam_get_stats(Scene *scene);

    void roam_set_move_input(Scene *scene, float x, float z);
    void roam_set_look_input(Scene *scene, float yaw_delta, float pitch_delta);
    void roam_set_jump(Scene *scene, bool pressed);
    Vec3 roam_get_camera_position(Scene *scene);
    Vec3 roam_get_camera_target(Scene *scene);
    Character *roam_get_player(Scene *scene);

#ifdef __cplusplus
}
#endif

#endif
