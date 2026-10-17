#include "terrain_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/rng.h"
#include "engine/voxel/chunk.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>

#define PILLAR_BASE_MULT 1.2f
#define PILLAR_TOP_MULT 0.8f
#define PILLAR_BASE_DEPTH 2.0f
#define STRUCTURE_MARGIN 2.0f
#define STRUCTURE_SEED 12345
#define PILLAR_HEIGHT_MIN 3.0f
#define PILLAR_HEIGHT_MAX 8.0f
#define PILLAR_RADIUS_MIN 0.3f
#define PILLAR_RADIUS_MAX 0.6f
/* A column is a cliff when a neighbour's surface is more than this many voxels off */
#define SLOPE_STEEP_THRESHOLD 2.0f
#define LAKE_COVER_SEED 12000u
#define LAKE_COVER_WAVELENGTH 4.0f
/* Columns between samples of the biome field, which is interpolated in between (its
 * inputs vary over a metre or more, see BiomeSample) */
#define BIOME_SAMPLE_STEP 4
/* Each column reads the biome field up to this many columns away in x and z, and its
 * relative altitude up to this much off, so neighbouring biomes, ground patches, the
 * mountain line and the snow caps dither into each other over a narrow band */
#define BIOME_DITHER_COLUMNS 2.5f
#define BIOME_DITHER_ALTITUDE 0.02f
#define BIOME_DITHER_SEED 31000u

float terrain_gen_height(float x, float z, float amplitude, float frequency, uint32_t seed)
{
    float base = noise_fbm_2d(x * frequency, z * frequency, seed, 6, 2.0f, 0.5f) * amplitude;

    float ridge = noise_ridged_2d(x * frequency * 0.5f, z * frequency * 0.5f,
                                   seed + 2000, 4, 2.2f, 0.5f);
    float ridge_mask = noise_value_2d(x * frequency * 0.1f, z * frequency * 0.1f, seed + 6000);
    ridge_mask = fmaxf(0.0f, ridge_mask);

    return base + ridge * ridge_mask * amplitude * 0.5f;
}

static BiomeSample biome_sample_lerp(BiomeSample a, BiomeSample b, float t)
{
    BiomeSample s;
    s.temperature = a.temperature + (b.temperature - a.temperature) * t;
    s.humidity = a.humidity + (b.humidity - a.humidity) * t;
    s.edge = a.edge + (b.edge - a.edge) * t;
    s.patch = a.patch + (b.patch - a.patch) * t;
    return s;
}

/* Biome field grid and column passes of terrain_columns_build. Every grid point and every
 * column is a pure function of its position and the seed, written to its own slot: any
 * split across threads gives the same arrays. */
typedef struct
{
    TerrainColumns *cols;
    const VoxelVolume *vol;
    BiomeField field;
    BiomeSample *grid; /* Every BIOME_SAMPLE_STEP columns, one point past the last */
    int32_t grid_nx;
    float amplitude, frequency;
} ColumnsBuild;

static void build_biome_grid_rows(void *ctx, int32_t begin, int32_t end)
{
    const ColumnsBuild *b = (const ColumnsBuild *)ctx;
    const float step = b->vol->voxel_size * BIOME_SAMPLE_STEP;
    for (int32_t jz = begin; jz < end; jz++)
        for (int32_t jx = 0; jx < b->grid_nx; jx++)
            b->grid[jz * b->grid_nx + jx] = biome_field_sample(&b->field, b->vol->bounds.min_x + jx * step,
                                                               b->vol->bounds.min_z + jz * step);
}

static void build_column_rows(void *ctx, int32_t begin, int32_t end)
{
    const ColumnsBuild *b = (const ColumnsBuild *)ctx;
    TerrainColumns *cols = b->cols;
    const VoxelVolume *vol = b->vol;
    const int32_t grid_nx = b->grid_nx;
    const int32_t grid_nz = (cols->nz - 1) / BIOME_SAMPLE_STEP + 2;
    const float inv_amplitude = b->amplitude > 0.0f ? 1.0f / b->amplitude : 0.0f;
    const float inv_step = 1.0f / BIOME_SAMPLE_STEP;
    const float max_jx = (float)(grid_nx - 1) - 1e-3f;
    const float max_jz = (float)(grid_nz - 1) - 1e-3f;
    for (int32_t gz = begin; gz < end; gz++)
    {
        float z = vol->bounds.min_z + gz * vol->voxel_size;
        for (int32_t gx = 0; gx < cols->nx; gx++)
        {
            float x = vol->bounds.min_x + gx * vol->voxel_size;

            RngState dither = terrain_cell_rng(cols->seed + BIOME_DITHER_SEED, gx, gz);
            float fx = ((float)gx + rng_range_f32(&dither, -BIOME_DITHER_COLUMNS, BIOME_DITHER_COLUMNS)) * inv_step;
            float fz = ((float)gz + rng_range_f32(&dither, -BIOME_DITHER_COLUMNS, BIOME_DITHER_COLUMNS)) * inv_step;
            fx = clampf(fx, 0.0f, max_jx);
            fz = clampf(fz, 0.0f, max_jz);
            int32_t jx = (int32_t)fx;
            int32_t jz = (int32_t)fz;
            const BiomeSample *g = b->grid + jz * grid_nx + jx;
            float tx = fx - (float)jx;
            BiomeSample sample = biome_sample_lerp(biome_sample_lerp(g[0], g[1], tx),
                                                   biome_sample_lerp(g[grid_nx], g[grid_nx + 1], tx), fz - (float)jz);

            float h = terrain_gen_height(x, z, b->amplitude, b->frequency, cols->seed);
            float altitude = h * inv_amplitude + rng_range_f32(&dither, -BIOME_DITHER_ALTITUDE, BIOME_DITHER_ALTITUDE);
            BiomeColumn column = biome_resolve(sample, altitude);

            int32_t i = terrain_column_index(cols, gx, gz);
            cols->height[i] = TERRAIN_BASE_HEIGHT + h;
            cols->biome[i] = (uint8_t)column.biome;
            cols->surface[i] = column.surface;
        }
    }
}

bool terrain_columns_build(TerrainColumns *cols, const VoxelVolume *vol, float amplitude,
                           float frequency, uint32_t seed, JobSystem *jobs)
{
    cols->seed = seed;
    cols->nx = vol->chunks_x * CHUNK_SIZE;
    cols->nz = vol->chunks_z * CHUNK_SIZE;
    const size_t count = (size_t)cols->nx * (size_t)cols->nz;
    cols->height = (float *)malloc(count * sizeof(float));
    cols->biome = (uint8_t *)malloc(count);
    cols->surface = (uint8_t *)malloc(count);

    ColumnsBuild build;
    build.cols = cols;
    build.vol = vol;
    build.field = biome_field_create(seed);
    build.grid_nx = (cols->nx - 1) / BIOME_SAMPLE_STEP + 2;
    build.amplitude = amplitude;
    build.frequency = frequency;
    const int32_t grid_nz = (cols->nz - 1) / BIOME_SAMPLE_STEP + 2;
    build.grid = (BiomeSample *)malloc((size_t)build.grid_nx * (size_t)grid_nz * sizeof(BiomeSample));
    if (!cols->height || !cols->biome || !cols->surface || !build.grid)
    {
        free(build.grid);
        terrain_columns_release(cols);
        return false;
    }

    /* The grid is complete before any column interpolates it */
    job_parallel_for(jobs, grid_nz, 0, build_biome_grid_rows, &build);
    job_parallel_for(jobs, cols->nz, 0, build_column_rows, &build);

    free(build.grid);
    return true;
}

void terrain_columns_release(TerrainColumns *cols)
{
    free(cols->height);
    free(cols->biome);
    free(cols->surface);
    cols->height = NULL;
    cols->biome = NULL;
    cols->surface = NULL;
    cols->nx = 0;
    cols->nz = 0;
}

int32_t terrain_column_at(const TerrainColumns *cols, const VoxelVolume *vol, float x, float z)
{
    /* Rounded as volume_world_to_voxel rounds, so the column is the one holding the voxel
     * at (x, z) */
    int32_t gx = (int32_t)floorf((x - vol->bounds.min_x) * vol->inv_voxel_size);
    int32_t gz = (int32_t)floorf((z - vol->bounds.min_z) * vol->inv_voxel_size);
    gx = gx < 0 ? 0 : (gx >= cols->nx ? cols->nx - 1 : gx);
    gz = gz < 0 ? 0 : (gz >= cols->nz ? cols->nz - 1 : gz);
    return terrain_column_index(cols, gx, gz);
}

typedef struct
{
    VoxelVolume *vol;
    const TerrainColumns *cols;
} TerrainFill;

/* Chunk column cc of a volume: the stack of chunks at one (cx, cz). Stages that write
 * voxels split the volume into chunk columns, so no two threads ever write the same
 * chunk (voxels, occupancy, upload state). */
static VoxelBox chunk_column_box(const VoxelVolume *vol, int32_t cc)
{
    VoxelBox box;
    box.min_x = (cc % vol->chunks_x) * CHUNK_SIZE;
    box.min_z = (cc / vol->chunks_x) * CHUNK_SIZE;
    box.min_y = 0;
    box.max_x = box.min_x + CHUNK_SIZE - 1;
    box.max_z = box.min_z + CHUNK_SIZE - 1;
    box.max_y = vol->chunks_y * CHUNK_SIZE - 1;
    return box;
}

/* Fills chunk columns with the ground layers. The volume is bulk written, so the voxels
 * are stored directly and each chunk's occupancy is rebuilt once afterwards instead of
 * being kept up per voxel. */
static void heightmap_fill_chunk_columns(void *ctx, int32_t begin, int32_t end)
{
    const TerrainFill *f = (const TerrainFill *)ctx;
    VoxelVolume *vol = f->vol;
    const TerrainColumns *cols = f->cols;
    const float voxel_size = vol->voxel_size;
    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;

    for (int32_t cc = begin; cc < end; cc++)
    {
        const VoxelBox box = chunk_column_box(vol, cc);
        int32_t filled_y = 0;
        for (int32_t gz = box.min_z; gz <= box.max_z; gz++)
        {
            for (int32_t gx = box.min_x; gx <= box.max_x; gx++)
            {
                int32_t i = terrain_column_index(cols, gx, gz);
                const BiomeDesc *biome = &g_biomes[cols->biome[i]];
                float surface_y = cols->height[i];

                /* Steepest step to the four neighbours (none past the edges), so a cliff
                 * shows rock whichever way it faces */
                float step = 0.0f;
                if (gx > 0)
                    step = fmaxf(step, fabsf(cols->height[i - 1] - surface_y));
                if (gx + 1 < cols->nx)
                    step = fmaxf(step, fabsf(cols->height[i + 1] - surface_y));
                if (gz > 0)
                    step = fmaxf(step, fabsf(cols->height[i - cols->nx] - surface_y));
                if (gz + 1 < cols->nz)
                    step = fmaxf(step, fabsf(cols->height[i + cols->nx] - surface_y));
                bool steep = step > voxel_size * SLOPE_STEEP_THRESHOLD;

                const uint8_t top = cols->surface[i];
                const float surface_depth = voxel_size * biome->surface_depth;
                const float soil_depth = voxel_size * biome->soil_depth;

                int32_t surface_gy = terrain_surface_voxel(vol, surface_y);
                if (surface_gy > total_y)
                    surface_gy = total_y;
                if (surface_gy > filled_y)
                    filled_y = surface_gy;

                for (int32_t gy = 0; gy < surface_gy; gy++)
                {
                    float depth = surface_y - (vol->bounds.min_y + gy * voxel_size);

                    /* Rock below the soil, and all the way up on cliffs */
                    uint8_t mat = biome->rock;
                    if (!steep && depth < surface_depth)
                        mat = top;
                    else if (!steep && depth < soil_depth)
                        mat = biome->subsurface;

                    Chunk *chunk = terrain_voxel_chunk(vol, gx, gy, gz);
                    chunk->voxels[chunk_voxel_index(gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK, gz & CHUNK_SIZE_MASK)]
                        .material = mat;
                }
            }
        }

        for (int32_t gy = 0; gy < filled_y; gy += CHUNK_SIZE)
        {
            Chunk *chunk = terrain_voxel_chunk(vol, box.min_x, gy, box.min_z);
            chunk_rebuild_occupancy(chunk);
            chunk->state = CHUNK_STATE_DIRTY;
        }
    }
}

void terrain_gen_heightmap(VoxelVolume *vol, const TerrainColumns *cols, JobSystem *jobs)
{
    if (cols->nx == 0 || cols->nz == 0)
        return;
    TerrainFill fill = {vol, cols};
    job_parallel_for(jobs, vol->chunks_x * vol->chunks_z, 0, heightmap_fill_chunk_columns, &fill);
}

/* Lowers one inner column to the lowest neighbouring water level it is not below.
 * Returns true if it changed. */
static bool relax_water_level(const float *terrain_h, float *water_h, int32_t i, int32_t nx)
{
    float level = fminf(fminf(water_h[i - 1], water_h[i + 1]), fminf(water_h[i - nx], water_h[i + nx]));
    level = fmaxf(level, terrain_h[i]);
    if (level < water_h[i])
    {
        water_h[i] = level;
        return true;
    }
    return false;
}

/* The water levels are the greatest fixed point of the relaxation: levels only drop, to
 * values copied from the grid, and every order of relaxing columns that runs until no
 * column changes ends at that same point, bit for bit. The grid is cut into blocks that
 * relax in two checkerboard colours: a block reads only its own columns and the edge
 * columns of its four neighbours, which have the other colour and stand still while it
 * runs, so the blocks of one colour run in parallel without sharing a column. */
#define FLOOD_BLOCK 64

typedef struct
{
    const float *terrain_h;
    float *water_h;
    int32_t nx, nz;
    float sea_level;
    int32_t blocks_x, blocks_z;
    const int32_t *run;  /* Blocks of this pass */
    uint8_t *lowered;    /* Per block: its last run lowered a level */
} FloodLevels;

static void flood_init_rows(void *ctx, int32_t begin, int32_t end)
{
    const FloodLevels *f = (const FloodLevels *)ctx;
    for (int32_t gz = begin; gz < end; gz++)
    {
        for (int32_t gx = 0; gx < f->nx; gx++)
        {
            int32_t i = gz * f->nx + gx;
            bool boundary = (gx == 0 || gx == f->nx - 1 || gz == 0 || gz == f->nz - 1);
            f->water_h[i] = boundary ? fmaxf(f->terrain_h[i], f->sea_level) : FLT_MAX;
        }
    }
}

/* Relaxes the inner columns of each listed block until none changes. Alternating the
 * sweep direction carries a drain level across a block in a pass or two whichever way
 * the path to it runs. */
static void flood_relax_blocks(void *ctx, int32_t begin, int32_t end)
{
    const FloodLevels *f = (const FloodLevels *)ctx;
    for (int32_t r = begin; r < end; r++)
    {
        const int32_t block = f->run[r];
        const int32_t x0 = (block % f->blocks_x) * FLOOD_BLOCK;
        const int32_t z0 = (block / f->blocks_x) * FLOOD_BLOCK;
        const int32_t first_x = x0 > 1 ? x0 : 1;
        const int32_t first_z = z0 > 1 ? z0 : 1;
        const int32_t last_x = x0 + FLOOD_BLOCK - 1 < f->nx - 2 ? x0 + FLOOD_BLOCK - 1 : f->nx - 2;
        const int32_t last_z = z0 + FLOOD_BLOCK - 1 < f->nz - 2 ? z0 + FLOOD_BLOCK - 1 : f->nz - 2;

        bool lowered = false;
        bool changed = true;
        for (int32_t pass = 0; changed; pass++)
        {
            changed = false;
            if (pass % 2 == 0)
            {
                for (int32_t gz = first_z; gz <= last_z; gz++)
                    for (int32_t gx = first_x; gx <= last_x; gx++)
                        changed |= relax_water_level(f->terrain_h, f->water_h, gz * f->nx + gx, f->nx);
            }
            else
            {
                for (int32_t gz = last_z; gz >= first_z; gz--)
                    for (int32_t gx = last_x; gx >= first_x; gx--)
                        changed |= relax_water_level(f->terrain_h, f->water_h, gz * f->nx + gx, f->nx);
            }
            lowered |= changed;
        }
        f->lowered[block] = lowered;
    }
}

bool terrain_flood_water_levels(const float *terrain_h, float *water_h,
                                int32_t nx, int32_t nz, float sea_level, JobSystem *jobs)
{
    FloodLevels f;
    f.terrain_h = terrain_h;
    f.water_h = water_h;
    f.nx = nx;
    f.nz = nz;
    f.sea_level = sea_level;
    f.blocks_x = (nx + FLOOD_BLOCK - 1) / FLOOD_BLOCK;
    f.blocks_z = (nz + FLOOD_BLOCK - 1) / FLOOD_BLOCK;
    const int32_t block_count = f.blocks_x * f.blocks_z;
    int32_t *run = (int32_t *)malloc((size_t)block_count * sizeof(int32_t));
    f.lowered = (uint8_t *)calloc((size_t)block_count, 1);
    f.run = run;

    if (!run || !f.lowered)
    {
        free(run);
        free(f.lowered);
        return false;
    }
    job_parallel_for(jobs, nz, 0, flood_init_rows, &f);

    /* Each colour first runs whole; afterwards a block runs again only when a neighbour
     * lowered a level in the pass before (its own columns are settled otherwise). A pass
     * with nothing to run means no block's input changed since it settled: done. */
    for (int32_t pass = 0;; pass++)
    {
        const int32_t colour = pass % 2;
        int32_t run_count = 0;
        for (int32_t bz = 0; bz < f.blocks_z; bz++)
        {
            for (int32_t bx = 0; bx < f.blocks_x; bx++)
            {
                if ((bx + bz) % 2 != colour)
                    continue;
                int32_t b = bz * f.blocks_x + bx;
                bool due = pass < 2 || (bx > 0 && f.lowered[b - 1]) || (bx + 1 < f.blocks_x && f.lowered[b + 1]) ||
                           (bz > 0 && f.lowered[b - f.blocks_x]) || (bz + 1 < f.blocks_z && f.lowered[b + f.blocks_x]);
                if (due)
                    run[run_count++] = b;
            }
        }
        if (run_count == 0)
            break;

        /* The flags of this colour now describe this pass only */
        for (int32_t bz = 0; bz < f.blocks_z; bz++)
            for (int32_t bx = (bz + colour) % 2; bx < f.blocks_x; bx += 2)
                f.lowered[bz * f.blocks_x + bx] = 0;
        job_parallel_for(jobs, run_count, 1, flood_relax_blocks, &f);
    }

    free(run);
    free(f.lowered);
    return true;
}

typedef struct
{
    VoxelVolume *vol;
    const TerrainColumns *cols;
    const float *water_h;
} WaterFill;

/* Fills the empty voxels between each column's terrain and its water level */
static void water_fill_chunk_columns(void *ctx, int32_t begin, int32_t end)
{
    const WaterFill *f = (const WaterFill *)ctx;
    VoxelVolume *vol = f->vol;
    const TerrainColumns *cols = f->cols;
    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    const float cover_f = 1.0f / LAKE_COVER_WAVELENGTH;

    for (int32_t cc = begin; cc < end; cc++)
    {
        const VoxelBox box = chunk_column_box(vol, cc);
        for (int32_t gz = box.min_z; gz <= box.max_z; gz++)
        {
            for (int32_t gx = box.min_x; gx <= box.max_x; gx++)
            {
                int32_t i = terrain_column_index(cols, gx, gz);
                float wh = f->water_h[i];
                float th = cols->height[i];
                if (wh <= th + vol->voxel_size * 0.5f)
                    continue;

                int32_t terrain_gy = terrain_surface_voxel(vol, th);
                int32_t water_gy = terrain_surface_voxel(vol, wh);
                if (terrain_gy < 0)
                    terrain_gy = 0;
                if (water_gy > total_y)
                    water_gy = total_y;

                const BiomeDesc *biome = &g_biomes[cols->biome[i]];
                for (int32_t gy = terrain_gy; gy < water_gy; gy++)
                {
                    if (terrain_voxel_get(vol, gx, gy, gz) == MAT_AIR)
                        terrain_voxel_set(vol, gx, gy, gz, biome->water);
                }

                int32_t top_gy = water_gy - 1;
                if (biome->lake_cover == MAT_AIR || top_gy < terrain_gy ||
                    terrain_voxel_get(vol, gx, top_gy, gz) != biome->water)
                    continue;
                if (biome->lake_cover_threshold > -1.0f)
                {
                    float x = vol->bounds.min_x + gx * vol->voxel_size;
                    float z = vol->bounds.min_z + gz * vol->voxel_size;
                    if (noise_value_2d(x * cover_f, z * cover_f, cols->seed + LAKE_COVER_SEED) <=
                        biome->lake_cover_threshold)
                        continue;
                }
                terrain_voxel_set(vol, gx, top_gy, gz, biome->lake_cover);
            }
        }
    }
}

void terrain_gen_water(VoxelVolume *vol, const TerrainColumns *cols, JobSystem *jobs)
{
    if (cols->nx == 0 || cols->nz == 0)
        return;
    float *water_h = (float *)malloc((size_t)cols->nx * (size_t)cols->nz * sizeof(float));
    if (water_h && terrain_flood_water_levels(cols->height, water_h, cols->nx, cols->nz, TERRAIN_SEA_LEVEL, jobs))
    {
        WaterFill fill = {vol, cols, water_h};
        job_parallel_for(jobs, vol->chunks_x * vol->chunks_z, 0, water_fill_chunk_columns, &fill);
    }
    free(water_h);
}

/* A round pillar of voxel layers standing on the voxel holding base, wider at its foot
 * and narrower at its top; the parts outside the volume are cut off */
static void generate_pillar(VoxelVolume *vol, Vec3 base, float height, float radius, uint8_t material)
{
    const float voxel_size = vol->voxel_size;
    const int32_t dims_x = vol->chunks_x * CHUNK_SIZE;
    const int32_t dims_y = vol->chunks_y * CHUNK_SIZE;
    const int32_t dims_z = vol->chunks_z * CHUNK_SIZE;
    /* Rounded as volume_world_to_voxel rounds; a base below the volume still raises the
     * pillar's upper layers */
    const int32_t base_x = (int32_t)floorf((base.x - vol->bounds.min_x) * vol->inv_voxel_size);
    const int32_t base_y = (int32_t)floorf((base.y - vol->bounds.min_y) * vol->inv_voxel_size);
    const int32_t base_z = (int32_t)floorf((base.z - vol->bounds.min_z) * vol->inv_voxel_size);
    const int32_t layers = (int32_t)ceilf(height * vol->inv_voxel_size);

    for (int32_t layer = 0; layer < layers; layer++)
    {
        const int32_t gy = base_y + layer;
        if (gy < 0 || gy >= dims_y)
            continue;
        const float y = (float)layer * voxel_size;
        float r = radius;
        if (y < voxel_size * PILLAR_BASE_DEPTH)
            r = radius * PILLAR_BASE_MULT;
        if (y > height - voxel_size * PILLAR_BASE_DEPTH)
            r = radius * PILLAR_TOP_MULT;

        const int32_t reach = (int32_t)(r * vol->inv_voxel_size);
        for (int32_t dz = -reach; dz <= reach; dz++)
        {
            for (int32_t dx = -reach; dx <= reach; dx++)
            {
                const float ox = (float)dx * voxel_size;
                const float oz = (float)dz * voxel_size;
                const int32_t gx = base_x + dx;
                const int32_t gz = base_z + dz;
                if (ox * ox + oz * oz <= r * r && gx >= 0 && gx < dims_x && gz >= 0 && gz < dims_z)
                    terrain_voxel_set(vol, gx, gy, gz, material);
            }
        }
    }
}

void terrain_gen_pillars(VoxelVolume *vol, int32_t count,
                          float amplitude, float frequency, uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + STRUCTURE_SEED);

    float margin = STRUCTURE_MARGIN;
    float area_min_x = vol->bounds.min_x + margin;
    float area_max_x = vol->bounds.max_x - margin;
    float area_min_z = vol->bounds.min_z + margin;
    float area_max_z = vol->bounds.max_z - margin;

    for (int32_t i = 0; i < count; i++)
    {
        float x = rng_range_f32(&rng, area_min_x, area_max_x);
        float z = rng_range_f32(&rng, area_min_z, area_max_z);
        float base_y = TERRAIN_BASE_HEIGHT + terrain_gen_height(x, z, amplitude, frequency, seed);

        float height = rng_range_f32(&rng, PILLAR_HEIGHT_MIN, PILLAR_HEIGHT_MAX);
        float radius = rng_range_f32(&rng, PILLAR_RADIUS_MIN, PILLAR_RADIUS_MAX);
        uint8_t mat = (uint8_t)(MAT_PASTEL_FIRST + rng_range_u32(&rng, MAT_PASTEL_COUNT));

        Vec3 base = vec3_create(x, base_y, z);
        generate_pillar(vol, base, height, radius, mat);
    }
}

/* ---- Trees ---- */

#define TREE_SEED_OFFSET 77777
#define TREE_GROVE_SEED 5000
#define TREE_GROVE_WAVELENGTH 18.0f
#define TREE_SPACING 3.5f
#define TREE_MARGIN 3.0f
#define TREE_SCALE 0.2f
#define TREE_MAX_DEPTH_SMALL 3
/* Columns around a trunk base, and voxel layers above their terrain, that must be empty */
#define TREE_CLEARANCE_RADIUS 2
#define TREE_CLEARANCE_HEIGHT 3

static void tree_grid(const VoxelVolume *vol, int32_t *cells_x, int32_t *cells_z)
{
    *cells_x = (int32_t)ceilf((vol->bounds.max_x - vol->bounds.min_x - 2.0f * TREE_MARGIN) / TREE_SPACING);
    *cells_z = (int32_t)ceilf((vol->bounds.max_z - vol->bounds.min_z - 2.0f * TREE_MARGIN) / TREE_SPACING);
}

static TreeType pick_species(const BiomeDesc *biome, RngState *rng)
{
    float total = 0.0f;
    for (int32_t i = 0; i < BIOME_MAX_TREES; i++)
        total += biome->trees[i].weight;

    float roll = rng_float(rng) * total;
    TreeType picked = biome->trees[0].species;
    for (int32_t i = 0; i < BIOME_MAX_TREES; i++)
    {
        if (biome->trees[i].weight <= 0.0f)
            continue;
        picked = biome->trees[i].species;
        roll -= biome->trees[i].weight;
        if (roll < 0.0f)
            break;
    }
    return picked;
}

/* Nothing (water, a pillar, another tree) stands just above the terrain around a column */
static bool tree_spot_is_clear(const VoxelVolume *vol, const TerrainColumns *cols, int32_t gx, int32_t gz)
{
    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    for (int32_t z = gz - TREE_CLEARANCE_RADIUS; z <= gz + TREE_CLEARANCE_RADIUS; z++)
    {
        for (int32_t x = gx - TREE_CLEARANCE_RADIUS; x <= gx + TREE_CLEARANCE_RADIUS; x++)
        {
            if (x < 0 || x >= cols->nx || z < 0 || z >= cols->nz)
                continue;
            int32_t first = terrain_surface_voxel(vol, cols->height[terrain_column_index(cols, x, z)]);
            if (first < 0)
                first = 0;
            for (int32_t gy = first; gy < first + TREE_CLEARANCE_HEIGHT && gy < total_y; gy++)
            {
                if (terrain_voxel_get(vol, x, gy, z) != MAT_AIR)
                    return false;
            }
        }
    }
    return true;
}

typedef struct
{
    const VoxelVolume *vol;
    const TerrainColumns *cols;
    float tree_density;
    int32_t cells_x;
    TreePlacement *spots; /* Per grid cell */
    uint8_t *planted;     /* Per grid cell */
} TreePlan;

/* Each spot draws from its own stream and reads the volume as it was before any tree
 * grew, so cells are planned in any order, on any thread, with the same outcome */
static void plan_tree_cells(void *ctx, int32_t begin, int32_t end)
{
    const TreePlan *plan = (const TreePlan *)ctx;
    const VoxelVolume *vol = plan->vol;
    const TerrainColumns *cols = plan->cols;
    const float grove_f = 1.0f / TREE_GROVE_WAVELENGTH;

    for (int32_t cell = begin; cell < end; cell++)
    {
        const int32_t ix = cell % plan->cells_x;
        const int32_t iz = cell / plan->cells_x;
        plan->planted[cell] = 0;

        RngState rng = terrain_cell_rng(cols->seed + TREE_SEED_OFFSET, ix, iz);
        float x = vol->bounds.min_x + TREE_MARGIN + ix * TREE_SPACING;
        float z = vol->bounds.min_z + TREE_MARGIN + iz * TREE_SPACING;
        float jx = x + rng_range_f32(&rng, -TREE_SPACING * 0.4f, TREE_SPACING * 0.4f);
        float jz = z + rng_range_f32(&rng, -TREE_SPACING * 0.4f, TREE_SPACING * 0.4f);

        int32_t i = terrain_column_at(cols, vol, jx, jz);
        const BiomeDesc *biome = &g_biomes[cols->biome[i]];

        /* Groves and clearings: the chance swings around the biome's mean */
        float grove = (noise_value_2d(jx * grove_f, jz * grove_f, cols->seed + TREE_GROVE_SEED) + 1.0f) * 0.5f;
        float chance = biome->tree_chance * plan->tree_density * (0.35f + 1.3f * grove);
        if (rng_float(&rng) >= chance)
            continue;

        if (!tree_spot_is_clear(vol, cols, i % cols->nx, i / cols->nx))
            continue;

        TreePlacement *p = &plan->spots[cell];
        p->base = vec3_create(jx, cols->height[i], jz);
        p->species = pick_species(biome, &rng);
        p->shape_seed = rng_next(&rng);
        plan->planted[cell] = 1;
    }
}

int32_t terrain_plan_trees(const VoxelVolume *vol, const TerrainColumns *cols, float tree_density,
                           TreePlacement *out, int32_t max_out, JobSystem *jobs)
{
    if (cols->nx == 0 || cols->nz == 0)
        return 0;

    TreePlan plan;
    int32_t cells_z;
    tree_grid(vol, &plan.cells_x, &cells_z);
    const int32_t cell_count = plan.cells_x * cells_z;
    plan.vol = vol;
    plan.cols = cols;
    plan.tree_density = tree_density;
    plan.spots = (TreePlacement *)malloc((size_t)cell_count * sizeof(TreePlacement));
    plan.planted = (uint8_t *)malloc((size_t)cell_count);
    if (!plan.spots || !plan.planted)
    {
        free(plan.spots);
        free(plan.planted);
        return 0;
    }

    job_parallel_for(jobs, cell_count, 0, plan_tree_cells, &plan);

    /* Cell order: the plan is the same whatever thread planned which cell */
    int32_t count = 0;
    for (int32_t cell = 0; cell < cell_count && count < max_out; cell++)
        if (plan.planted[cell])
            out[count++] = plan.spots[cell];

    free(plan.spots);
    free(plan.planted);
    return count;
}

/* Trees grow in parallel by chunk column. Crowns overlap, and where they do the result
 * depends on order (bark overwrites, leaves fill only empty voxels), so each chunk column
 * grows every tree reaching into it in plan order, clipped to itself: every voxel sees
 * the writes of the trees in the same order as if they grew one after the other, and the
 * volume comes out the same for any thread count. A tree reaching into k chunk columns is
 * generated k times; its branches cost a fraction of a percent of its voxels. */
typedef struct
{
    VoxelVolume *vol;
    uint32_t seed; /* World seed: the leaf noise */
    const TreePlacement *plan;
    int32_t tree_count;
    /* Chunk columns each tree reaches (inclusive), from its generated bounds */
    int32_t *first_cx, *last_cx, *first_cz, *last_cz;
    /* Trees reaching each chunk column, in plan order: tree_list[tree_start[cc]..tree_start[cc + 1]) */
    int32_t *tree_start;
    int32_t *tree_list;
    /* Chunk columns that some tree reaches */
    int32_t *busy;
} TreeGrowth;

static TreeConfig tree_growth_config(const TreePlacement *p, RngState *rng)
{
    rng_seed(rng, p->shape_seed);
    TreeConfig config = tree_config_create(p->species, rng, TREE_SCALE);
    if (config.max_depth > TREE_MAX_DEPTH_SMALL)
        config.max_depth = TREE_MAX_DEPTH_SMALL;
    return config;
}

/* Index range of the chunks holding voxel centres in [lo, hi] along one axis */
static bool chunk_span(float lo, float hi, float vol_min, float voxel_size, int32_t chunks,
                       int32_t *first, int32_t *last)
{
    int32_t v_first = (int32_t)ceilf((lo - vol_min) / voxel_size - 0.5f);
    int32_t v_last = (int32_t)floorf((hi - vol_min) / voxel_size - 0.5f);
    if (v_first < 0)
        v_first = 0;
    if (v_last > chunks * CHUNK_SIZE - 1)
        v_last = chunks * CHUNK_SIZE - 1;
    *first = v_first >> CHUNK_SIZE_BITS;
    *last = v_last >> CHUNK_SIZE_BITS;
    return v_first <= v_last;
}

static void measure_trees(void *ctx, int32_t begin, int32_t end)
{
    const TreeGrowth *g = (const TreeGrowth *)ctx;
    const VoxelVolume *vol = g->vol;
    ProceduralTree *tree = (ProceduralTree *)malloc(sizeof(ProceduralTree));
    for (int32_t t = begin; t < end; t++)
    {
        g->first_cx[t] = 0;
        g->last_cx[t] = -1;
        g->first_cz[t] = 0;
        g->last_cz[t] = -1;
        if (!tree)
            continue;

        RngState rng;
        TreeConfig config = tree_growth_config(&g->plan[t], &rng);
        tree_generate(tree, &config, &rng);
        if (tree->trunk_idx < 0)
            continue;

        /* The trunk's box spans the whole tree */
        const Bounds3D *box = &tree->branches[tree->trunk_idx].aabb;
        const Vec3 o = g->plan[t].base;
        int32_t fx, lx, fz, lz;
        if (chunk_span(o.x + box->min_x, o.x + box->max_x, vol->bounds.min_x, vol->voxel_size, vol->chunks_x, &fx, &lx) &&
            chunk_span(o.z + box->min_z, o.z + box->max_z, vol->bounds.min_z, vol->voxel_size, vol->chunks_z, &fz, &lz))
        {
            g->first_cx[t] = fx;
            g->last_cx[t] = lx;
            g->first_cz[t] = fz;
            g->last_cz[t] = lz;
        }
    }
    free(tree);
}

static void grow_trees_in_chunk_columns(void *ctx, int32_t begin, int32_t end)
{
    const TreeGrowth *g = (const TreeGrowth *)ctx;
    ProceduralTree *tree = (ProceduralTree *)malloc(sizeof(ProceduralTree));
    if (!tree)
        return;
    for (int32_t b = begin; b < end; b++)
    {
        const int32_t cc = g->busy[b];
        const VoxelBox clip = chunk_column_box(g->vol, cc);
        for (int32_t k = g->tree_start[cc]; k < g->tree_start[cc + 1]; k++)
        {
            const TreePlacement *p = &g->plan[g->tree_list[k]];
            RngState rng;
            TreeConfig config = tree_growth_config(p, &rng);
            tree_generate(tree, &config, &rng);
            tree_voxelize_clipped(tree, &config, g->vol, p->base, &clip, g->seed);
        }
    }
    free(tree);
}

/* Grows the planned trees (g->plan, g->tree_count); false if out of memory */
static bool grow_planned_trees(TreeGrowth *g, JobSystem *jobs)
{
    const VoxelVolume *vol = g->vol;
    const int32_t column_count = vol->chunks_x * vol->chunks_z;
    job_parallel_for(jobs, g->tree_count, 0, measure_trees, g);

    /* Bucket the trees by chunk column, keeping plan order inside each bucket: count,
     * prefix-sum to bucket starts, fill through those starts as cursors (each ends at
     * the next bucket's start), then shift the starts back */
    for (int32_t t = 0; t < g->tree_count; t++)
        for (int32_t cz = g->first_cz[t]; cz <= g->last_cz[t]; cz++)
            for (int32_t cx = g->first_cx[t]; cx <= g->last_cx[t]; cx++)
                g->tree_start[cz * vol->chunks_x + cx + 1]++;
    int32_t busy_count = 0;
    for (int32_t cc = 0; cc < column_count; cc++)
    {
        if (g->tree_start[cc + 1] > 0)
            g->busy[busy_count++] = cc;
        g->tree_start[cc + 1] += g->tree_start[cc];
    }
    const int32_t entries = g->tree_start[column_count];
    g->tree_list = (int32_t *)malloc((size_t)(entries > 0 ? entries : 1) * sizeof(int32_t));
    if (!g->tree_list)
        return false;
    for (int32_t t = 0; t < g->tree_count; t++)
        for (int32_t cz = g->first_cz[t]; cz <= g->last_cz[t]; cz++)
            for (int32_t cx = g->first_cx[t]; cx <= g->last_cx[t]; cx++)
                g->tree_list[g->tree_start[cz * vol->chunks_x + cx]++] = t;
    for (int32_t cc = column_count; cc > 0; cc--)
        g->tree_start[cc] = g->tree_start[cc - 1];
    g->tree_start[0] = 0;

    job_parallel_for(jobs, busy_count, 0, grow_trees_in_chunk_columns, g);
    free(g->tree_list);
    return true;
}

int32_t terrain_gen_trees(VoxelVolume *vol, const TerrainColumns *cols, float tree_density, JobSystem *jobs)
{
    if (cols->nx == 0 || cols->nz == 0)
        return 0;
    int32_t cells_x, cells_z;
    tree_grid(vol, &cells_x, &cells_z);
    const int32_t max_trees = cells_x * cells_z;
    const int32_t column_count = vol->chunks_x * vol->chunks_z;

    TreeGrowth g = {0};
    g.vol = vol;
    g.seed = cols->seed;
    TreePlacement *plan = (TreePlacement *)malloc((size_t)max_trees * sizeof(TreePlacement));
    int32_t *bounds = (int32_t *)malloc((size_t)max_trees * 4 * sizeof(int32_t));
    g.tree_start = (int32_t *)calloc((size_t)column_count + 1, sizeof(int32_t));
    g.busy = (int32_t *)malloc((size_t)column_count * sizeof(int32_t));

    int32_t count = 0;
    if (plan && bounds && g.tree_start && g.busy)
    {
        g.first_cx = bounds;
        g.last_cx = bounds + max_trees;
        g.first_cz = bounds + 2 * max_trees;
        g.last_cz = bounds + 3 * max_trees;

        /* Plan against the volume before any tree grows, then grow them all */
        count = terrain_plan_trees(vol, cols, tree_density, plan, max_trees, jobs);
        g.plan = plan;
        g.tree_count = count;
        if (!grow_planned_trees(&g, jobs))
            count = 0;
    }

    free(g.busy);
    free(g.tree_start);
    free(bounds);
    free(plan);
    return count;
}
