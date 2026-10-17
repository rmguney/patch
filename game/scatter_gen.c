#include "scatter_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/rng.h"
#include "engine/voxel/chunk.h"
#include <stdlib.h>

#define SCATTER_SEED_OFFSET 99999
#define SCATTER_PATCH_SEED 21000u
#define SCATTER_SLOPE_THRESHOLD 2

typedef struct
{
    VoxelVolume *vol;
    const TerrainColumns *cols;
    int32_t *placed; /* Flora voxels per chunk column */
} Scatter;

/* Top ground voxel of a column: under its trees, shrubs, water and lake cover */
static int32_t ground_voxel(const VoxelVolume *vol, const TerrainColumns *cols, int32_t i)
{
    return terrain_surface_voxel(vol, cols->height[i]) - 1;
}

/* Grows flora on the columns of whole chunk columns: each writes one voxel above its own
 * column, inside its own chunk column, and reads only the columns' heights and the
 * voxels just above the ground, which no other column writes */
static void grow_flora_chunk_columns(void *ctx, int32_t begin, int32_t end)
{
    const Scatter *sc = (const Scatter *)ctx;
    VoxelVolume *vol = sc->vol;
    const TerrainColumns *cols = sc->cols;
    const int32_t nx = cols->nx;
    const int32_t nz = cols->nz;
    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;

    for (int32_t cc = begin; cc < end; cc++)
    {
        const int32_t x0 = (cc % vol->chunks_x) * CHUNK_SIZE;
        const int32_t z0 = (cc / vol->chunks_x) * CHUNK_SIZE;
        int32_t placed = 0;
        for (int32_t gz = z0; gz < z0 + CHUNK_SIZE; gz++)
        {
            float z = vol->bounds.min_z + gz * vol->voxel_size;
            for (int32_t gx = x0; gx < x0 + CHUNK_SIZE; gx++)
            {
                int32_t i = terrain_column_index(cols, gx, gz);
                int32_t gy = ground_voxel(vol, cols, i);
                if (gy < 0 || gy + 1 >= total_y)
                    continue;

                /* Gentle ground only: no neighbour more than the threshold off */
                if ((gx > 0 && abs(ground_voxel(vol, cols, i - 1) - gy) > SCATTER_SLOPE_THRESHOLD) ||
                    (gx + 1 < nx && abs(ground_voxel(vol, cols, i + 1) - gy) > SCATTER_SLOPE_THRESHOLD) ||
                    (gz > 0 && abs(ground_voxel(vol, cols, i - nx) - gy) > SCATTER_SLOPE_THRESHOLD) ||
                    (gz + 1 < nz && abs(ground_voxel(vol, cols, i + nx) - gy) > SCATTER_SLOPE_THRESHOLD))
                    continue;

                /* Open to the air: not under water, a lake cover, a trunk or a shrub */
                if (terrain_voxel_get(vol, gx, gy + 1, gz) != MAT_AIR)
                    continue;

                const BiomeDesc *biome = &g_biomes[cols->biome[i]];
                uint8_t mat = terrain_voxel_get(vol, gx, gy, gz);
                float x = vol->bounds.min_x + gx * vol->voxel_size;

                /* One draw per column, split between the flora that can grow on its ground */
                float roll = -1.0f;
                for (int32_t f = 0; f < BIOME_MAX_FLORA; f++)
                {
                    const BiomeFlora *flora = &biome->flora[f];
                    if (flora->density <= 0.0f || flora->on_material != mat)
                        continue;

                    float density = flora->density;
                    if (flora->patch_wavelength > 0.0f)
                    {
                        /* Inside a patch the density ramps up from its edge */
                        float n = noise_value_2d(x / flora->patch_wavelength, z / flora->patch_wavelength,
                                                 cols->seed + SCATTER_PATCH_SEED + (uint32_t)flora->material * 7919u);
                        if (n <= flora->patch_threshold)
                            continue;
                        density *= (n - flora->patch_threshold) / (1.0f - flora->patch_threshold);
                    }

                    if (roll < 0.0f)
                    {
                        RngState rng = terrain_cell_rng(cols->seed + SCATTER_SEED_OFFSET, gx, gz);
                        roll = rng_float(&rng);
                    }
                    if (roll < density)
                    {
                        terrain_voxel_set(vol, gx, gy + 1, gz, flora->material);
                        placed++;
                        break;
                    }
                    roll -= density;
                }
            }
        }
        sc->placed[cc] = placed;
    }
}

int32_t scatter_gen_apply(VoxelVolume *vol, const TerrainColumns *cols, JobSystem *jobs)
{
    if (cols->nx == 0 || cols->nz == 0)
        return 0;

    const int32_t column_count = vol->chunks_x * vol->chunks_z;
    Scatter sc;
    sc.vol = vol;
    sc.cols = cols;
    sc.placed = (int32_t *)malloc((size_t)column_count * sizeof(int32_t));
    int32_t placed = 0;
    if (sc.placed)
    {
        job_parallel_for(jobs, column_count, 0, grow_flora_chunk_columns, &sc);
        for (int32_t cc = 0; cc < column_count; cc++)
            placed += sc.placed[cc];
    }
    free(sc.placed);
    return placed;
}
