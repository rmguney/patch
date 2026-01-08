#include "game/ball_pit_renderer.h"
#include "engine/render/renderer.h"

namespace patch
{

void ball_pit_render(Scene *scene, Renderer &renderer)
{
    (void)scene;
    (void)renderer;
    /* All rendering now handled by deferred raymarching pipeline in main.cpp:
     * - Terrain: render_gbuffer_terrain()
     * - Voxel objects: render_voxel_objects_raymarched()
     * - Particles: render_particles_raymarched()
     */
}

void ball_pit_render_objects(Scene *scene, Renderer &renderer)
{
    (void)scene;
    (void)renderer;
    /* Post-deferred pass - currently empty.
     * Could be used for transparent effects or UI overlays in future.
     */
}

}
