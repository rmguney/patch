#ifndef PATCH_GAME_TREE_GEN_H
#define PATCH_GAME_TREE_GEN_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define TREE_MAX_BRANCHES 2048

    typedef struct
    {
        float trunk_len;
        float trunk_radius;
        float branch_child_len;
        float branch_child_radius;
        bool branch_child_radius_lerp;
        float leaf_radius_min;
        float leaf_radius_max;
        float leaf_radius_scaled;
        float straightness;
        int32_t max_depth;
        float splits_min;
        float splits_max;
        float split_range_min;
        float split_range_max;
        float branch_len_bias;
        float leaf_vertical_scale;
        float proportionality;
        uint8_t bark_material;
        uint8_t leaf_material;
    } TreeConfig;

    typedef struct
    {
        Vec3 start;
        Vec3 end;
        float wood_radius;
        float leaf_radius;
        Bounds3D aabb; /* Covers the branch and all its descendants */
        int32_t sibling_idx;
        int32_t child_idx;
    } TreeBranch;

    /* Branches link to their last child and to their previous sibling */
    typedef struct
    {
        TreeBranch branches[TREE_MAX_BRANCHES];
        int32_t branch_count;
        int32_t trunk_idx; /* -1 when nothing was generated */
    } ProceduralTree;

    typedef enum
    {
        TREE_OAK,
        TREE_PINE,
        TREE_FROSTPINE,
        TREE_BIRCH,
        TREE_JUNGLE,
        TREE_BAOBAB,
        TREE_ACACIA,
        TREE_CHERRY,
        TREE_DEAD,
        TREE_MANGROVE,
        TREE_REDWOOD,
        TREE_CEDAR,
        TREE_MAPLE,
        TREE_PALM,
        TREE_SWAMP,
        TREE_GIANT,
        TREE_AUTUMN,
        TREE_CHESTNUT,
        TREE_TYPE_COUNT
    } TreeType;

    TreeConfig tree_config_create(TreeType type, RngState *rng, float scale);
    void tree_generate(ProceduralTree *tree, const TreeConfig *config, RngState *rng);
    /* Writes the tree standing at origin into the voxels inside clip (volume voxel
     * coordinates): bark over anything, leaves into empty voxels only. Voxelizing a tree
     * once per box of a partition of the volume writes exactly what one pass over the
     * whole volume does, so disjoint boxes can be voxelized on different threads. seed is
     * the world seed, which shapes the leaf crowns' noise. */
    void tree_voxelize_clipped(const ProceduralTree *tree, const TreeConfig *config, VoxelVolume *vol,
                               Vec3 origin, const VoxelBox *clip, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
