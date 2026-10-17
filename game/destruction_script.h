#ifndef PATCH_GAME_DESTRUCTION_SCRIPT_H
#define PATCH_GAME_DESTRUCTION_SCRIPT_H

#include "game/world_scene.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Scripted terrain destruction for headless test runs (--test-destruction): stands in
     * for a player digging, so the render perf test measures edit, upload and detach cost.
     * It drives a world scene scene through its public API only, and draws its targets from
     * the scene RNG ahead of the debris, so a seed replays the same destruction sequence.
     */
    typedef struct
    {
        int32_t interval_frames; /* destroy on every interval_frames-th frame; 0 = off */
        int32_t destructions;    /* steps whose ray found terrain */
        int32_t voxels_removed;
    } DestructionScript;

    /* Runs the script for counted test frame `frame` (from 1) of a world scene scene */
    void destruction_script_frame(DestructionScript *script, Scene *scene, int32_t frame);

    /* One scripted destruction: a ray straight down from the top of the world at a random
     * point of its central 60% (two scene RNG draws, x then z) carves a sphere where it
     * meets the terrain, liquids skipped. Returns false, changing nothing else, when the
     * ray finds no terrain. */
    bool destruction_script_step(Scene *scene, WorldSceneDestruction *out);

#ifdef __cplusplus
}
#endif

#endif
