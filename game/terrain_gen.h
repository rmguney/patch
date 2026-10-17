#ifndef PATCH_GAME_TERRAIN_GEN_H
#define PATCH_GAME_TERRAIN_GEN_H

#include "game/tree_gen.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/chunk.h"
#include "engine/core/jobs.h"
#include "engine/core/rng.h"
#include "engine/core/types.h"
#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define TERRAIN_BASE_HEIGHT 10.0f
#define TERRAIN_SEA_LEVEL 8.5f

    /* Per-column terrain of a volume, computed once by terrain_columns_build and read by
     * every later generation stage, so none of them evaluates the height or biome noise
     * again. Column (gx, gz) is the voxel column at x = min_x + gx * voxel_size (same for
     * z); arrays are row-major, x fastest.
     *
     * The generation stages take a job system (NULL: the calling thread alone) and give
     * the same volume for any thread count: work that writes voxels is split by chunk
     * column, so no two threads write the same chunk, and what each part computes depends
     * only on the seed and its own position, never on which thread ran it or when. */
    typedef struct
    {
        uint32_t seed; /* World seed the columns were built from (later stages draw from it) */
        int32_t nx, nz;
        float *height;    /* World y of the terrain surface */
        uint8_t *biome;   /* BiomeKind */
        uint8_t *surface; /* Top ground material: the biome's surface, patch or cap */
    } TerrainColumns;

    /* Where one tree grows (see terrain_plan_trees) */
    typedef struct
    {
        Vec3 base;
        TreeType species;
        uint64_t shape_seed; /* Seeds the tree's shape */
    } TreePlacement;

    float terrain_gen_height(float x, float z, float amplitude, float frequency, uint32_t seed);

    /* Each column's height, biome and top material; false if out of memory, leaving the
     * columns empty (nx = nz = 0), on which every generation stage writes nothing */
    bool terrain_columns_build(TerrainColumns *cols, const VoxelVolume *vol, float amplitude,
                               float frequency, uint32_t seed, JobSystem *jobs);
    void terrain_columns_release(TerrainColumns *cols);

    static inline int32_t terrain_column_index(const TerrainColumns *cols, int32_t gx, int32_t gz)
    {
        return gz * cols->nx + gx;
    }

    /* Column under a world position, clamped to the volume */
    int32_t terrain_column_at(const TerrainColumns *cols, const VoxelVolume *vol, float x, float z);

    /* First voxel layer above the terrain of a column of the given surface height */
    static inline int32_t terrain_surface_voxel(const VoxelVolume *vol, float surface_height)
    {
        return (int32_t)ceilf((surface_height - vol->bounds.min_y) / vol->voxel_size);
    }

    /* Voxel access by global voxel coordinates, which must lie inside the volume */
    static inline Chunk *terrain_voxel_chunk(const VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz)
    {
        return &vol->chunks[(gx >> CHUNK_SIZE_BITS) + (gy >> CHUNK_SIZE_BITS) * vol->chunks_x +
                            (gz >> CHUNK_SIZE_BITS) * vol->chunks_x * vol->chunks_y];
    }

    static inline uint8_t terrain_voxel_get(const VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz)
    {
        return chunk_get(terrain_voxel_chunk(vol, gx, gy, gz), gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK,
                         gz & CHUNK_SIZE_MASK);
    }

    static inline void terrain_voxel_set(VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz, uint8_t material)
    {
        chunk_set(terrain_voxel_chunk(vol, gx, gy, gz), gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK,
                  gz & CHUNK_SIZE_MASK, material);
    }

    /* Random stream of one generation cell (a tree spot, a shrub spot, a column): a pure
     * function of the seed and the cell, so cells can be generated in any order */
    static inline RngState terrain_cell_rng(uint32_t seed, int32_t cx, int32_t cz)
    {
        uint64_t h = ((uint64_t)seed << 32) ^ ((uint64_t)(uint32_t)cx * 0x9E3779B97F4A7C15ull) ^
                     ((uint64_t)(uint32_t)cz * 0xC2B2AE3D27D4EB4Full);
        /* splitmix64 finalizer: neighbouring cells get unrelated streams */
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        RngState rng;
        rng_seed(&rng, h);
        return rng;
    }

    /* Fills every column with its biome's ground layers up to the terrain surface */
    void terrain_gen_heightmap(VoxelVolume *vol, const TerrainColumns *cols, JobSystem *jobs);

    void terrain_gen_pillars(VoxelVolume *vol, int32_t count,
                             float amplitude, float frequency, uint32_t seed);

    /* Spill level of every column of an nx * nz height grid (row-major, x fastest):
     * edge columns drain at max(terrain, sea_level), and every inner column holds water
     * up to the lowest level at which it can flow off the map. Columns whose level is
     * above their terrain are under water. False if out of memory. */
    bool terrain_flood_water_levels(const float *terrain_h, float *water_h,
                                    int32_t nx, int32_t nz, float sea_level, JobSystem *jobs);

    /* Fills every lake and the sea (see terrain_flood_water_levels) with each column's biome
     * water, under the biome's lake cover (ice, duckweed) where it has one */
    void terrain_gen_water(VoxelVolume *vol, const TerrainColumns *cols, JobSystem *jobs);

    /* Tree spots for the volume as it is (call after the water): one candidate per grid
     * cell, kept with its biome's tree chance unless something already stands there; the
     * species is drawn from the biome's tree weights. Writes up to max_out placements and
     * returns how many it wrote. */
    int32_t terrain_plan_trees(const VoxelVolume *vol, const TerrainColumns *cols, float tree_density,
                               TreePlacement *out, int32_t max_out, JobSystem *jobs);

    /* Plans the trees (terrain_plan_trees) and grows them into the volume; returns the count */
    int32_t terrain_gen_trees(VoxelVolume *vol, const TerrainColumns *cols, float tree_density, JobSystem *jobs);

#ifdef __cplusplus
}
#endif

#endif
