#include "ball_pit_renderer.h"
#include "../engine/renderer.h"

namespace patch {

void ball_pit_render(Scene* scene, Renderer& renderer) {
    if (!scene || !scene->user_data) return;
    
    BallPitData* data = static_cast<BallPitData*>(scene->user_data);
    
    renderer.draw_pit(scene->bounds);
    renderer.draw_voxels(data->voxels->voxels, scene->bounds, data->voxels->voxel_size);
    renderer.draw_particles(data->particles);

    for (int32_t i = 0; i < data->vobj_world->object_count; i++) {
        renderer.draw_voxel_object(&data->vobj_world->objects[i]);
    }
}

}
