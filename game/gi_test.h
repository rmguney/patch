#ifndef PATCH_SCENES_GI_TEST_H
#define PATCH_SCENES_GI_TEST_H

#include "engine/sim/scene.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/physics/particles.h"
#include "engine/physics/rigidbody.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float voxel_size;

        VoxelVolume *terrain;
        VoxelObjectWorld *objects;
        ParticleSystem *particles;
        PhysicsWorld *physics;

        Vec3 ray_origin;
        Vec3 ray_dir;
    } GITestData;

    Scene *gi_test_scene_create(Bounds3D bounds, float voxel_size);
    void gi_test_scene_destroy(Scene *scene);

    void gi_test_set_ray(Scene *scene, Vec3 origin, Vec3 dir);

    VoxelVolume *gi_test_get_terrain(Scene *scene);
    VoxelObjectWorld *gi_test_get_objects(Scene *scene);
    ParticleSystem *gi_test_get_particles(Scene *scene);
    PhysicsWorld *gi_test_get_physics(Scene *scene);

#ifdef __cplusplus
}
#endif

#endif
