#ifndef PATCH_PHYSICS_CHARACTER_H
#define PATCH_PHYSICS_CHARACTER_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CHAR_CAPSULE_RADIUS 0.3f
#define CHAR_CAPSULE_HEIGHT 1.8f
#define CHAR_STEP_HEIGHT 0.3f
#define CHAR_GROUND_CHECK_DIST 0.1f
#define CHAR_SLIDE_ITERATIONS 3
#define CHAR_SAMPLE_POINTS 12

    typedef struct
    {
        Vec3 position;
        Vec3 velocity;
        float radius;
        float height;
        float step_height;
        bool is_grounded;
        bool is_sliding;
        Vec3 ground_normal;
    } Character;

    void character_init(Character *character, Vec3 start_position);

    void character_move(Character *character,
                        VoxelVolume *terrain,
                        VoxelObjectWorld *objects,
                        Vec3 move_input,
                        float dt);

    void character_jump(Character *character, float jump_velocity);

    bool character_is_grounded(Character *character);

    Vec3 character_get_feet_position(Character *character);
    Vec3 character_get_head_position(Character *character);

#ifdef __cplusplus
}
#endif

#endif
