#include "game/biome.h"
#include "game/terrain_gen.h"
#include "game/tree_gen.h"
#include "game/shrub_gen.h"
#include "game/scatter_gen.h"
#include "game/world_scene_internal.h"
#include "content/materials.h"
#include "content/scenes.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/chunk.h"
#include "engine/core/jobs.h"
#include "engine/platform/thread.h"
#include "engine/sim/env_particles.h"
#include "test_common.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_MAX_TREES 8192

/* ---- Biome tables ---- */

static bool is_registered(uint8_t mat)
{
    return mat != MAT_AIR && mat < g_material_count && material_get(mat) != NULL;
}

/* Solid, opaque, not liquid: something the ground can be made of */
static bool is_ground(uint8_t mat)
{
    if (!is_registered(mat))
        return false;
    uint8_t flags = material_get(mat)->flags;
    return (flags & MAT_FLAG_SOLID) && !(flags & MAT_FLAG_TRANSPARENT) && !(flags & MAT_FLAG_LIQUID);
}

TEST(biome_surfaces_distinct)
{
    /* Every biome must read differently from the air: no two share a surface material */
    for (int a = 0; a < BIOME_COUNT; a++)
        for (int b = a + 1; b < BIOME_COUNT; b++)
            ASSERT(g_biomes[a].surface != g_biomes[b].surface);
    return 1;
}

TEST(biome_ground_tables_valid)
{
    for (int b = 0; b < BIOME_COUNT; b++)
    {
        const BiomeDesc *d = &g_biomes[b];
        ASSERT(is_ground(d->surface));
        ASSERT(is_ground(d->subsurface));
        ASSERT(is_ground(d->rock));
        ASSERT(d->patch == MAT_AIR || (is_ground(d->patch) && d->patch != d->surface));
        ASSERT(d->patch == MAT_AIR || (d->patch_threshold > -1.0f && d->patch_threshold < 1.0f));
        ASSERT(d->cap == MAT_AIR || (is_ground(d->cap) && d->cap_altitude > BIOME_MOUNTAIN_ALTITUDE));
        ASSERT(d->lake_cover == MAT_AIR || is_ground(d->lake_cover));
        /* Water the renderer draws as such, whose colour it absorbs by */
        ASSERT(material_is_liquid(d->water) && (material_get(d->water)->flags & MAT_FLAG_TRANSPARENT) != 0);
        ASSERT(d->surface_depth >= 1.0f && d->surface_depth <= d->soil_depth);
    }
    return 1;
}

TEST(biome_tree_tables_valid)
{
    bool species_used[TREE_TYPE_COUNT] = {false};
    for (int b = 0; b < BIOME_COUNT; b++)
    {
        const BiomeDesc *d = &g_biomes[b];
        ASSERT(d->tree_chance >= 0.0f && d->tree_chance <= 1.0f);
        float total = 0.0f;
        for (int i = 0; i < BIOME_MAX_TREES; i++)
        {
            const BiomeTree *t = &d->trees[i];
            ASSERT(t->weight >= 0.0f);
            if (t->weight == 0.0f)
                continue;
            ASSERT((int)t->species >= 0 && t->species < TREE_TYPE_COUNT);
            /* A species is listed once per biome */
            for (int j = 0; j < i; j++)
                ASSERT(d->trees[j].weight == 0.0f || d->trees[j].species != t->species);
            species_used[t->species] = true;
            total += t->weight;
        }
        /* Trees grow exactly where the biome has species for them */
        ASSERT((total > 0.0f) == (d->tree_chance > 0.0f));
    }
    for (int s = 0; s < TREE_TYPE_COUNT; s++)
        ASSERT(species_used[s]);
    return 1;
}

TEST(biome_flora_tables_valid)
{
    for (int b = 0; b < BIOME_COUNT; b++)
    {
        const BiomeDesc *d = &g_biomes[b];
        ASSERT(d->shrub_chance >= 0.0f && d->shrub_chance <= 1.0f);
        float total = 0.0f;
        for (int i = 0; i < BIOME_MAX_SHRUBS; i++)
        {
            const BiomeShrub *s = &d->shrubs[i];
            ASSERT(s->weight >= 0.0f);
            if (s->weight == 0.0f)
                continue;
            ASSERT(is_registered(s->material));
            ASSERT(s->shape == SHRUB_DOME || s->shape == SHRUB_COLUMN || s->shape == SHRUB_STALKS);
            ASSERT(s->min_height >= 1 && s->min_height <= s->max_height);
            total += s->weight;
        }
        ASSERT((total > 0.0f) == (d->shrub_chance > 0.0f));

        for (int i = 0; i < BIOME_MAX_FLORA; i++)
        {
            const BiomeFlora *f = &d->flora[i];
            if (f->density == 0.0f)
                continue;
            ASSERT(f->density > 0.0f && f->density <= 1.0f);
            ASSERT(is_registered(f->material));
            ASSERT(!(material_get(f->material)->flags & MAT_FLAG_LIQUID));
            ASSERT(f->patch_wavelength >= 0.0f);
            ASSERT(f->patch_threshold > -1.0f && f->patch_threshold < 1.0f);
            /* It must grow on something this biome's ground is made of */
            ASSERT(f->on_material == d->surface || f->on_material == d->patch || f->on_material == d->cap ||
                   f->on_material == d->subsurface || f->on_material == d->rock);
        }
    }
    return 1;
}

TEST(biome_climate_reaches_every_biome)
{
    /* Every biome but the mountain owns part of the climate square, the mountain owns the heights */
    bool reached[BIOME_COUNT] = {false};
    for (float t = -1.0f; t <= 1.0f; t += 0.05f)
        for (float h = -1.0f; h <= 1.0f; h += 0.05f)
        {
            ASSERT(biome_classify(t, h, BIOME_MOUNTAIN_ALTITUDE + 0.01f) == BIOME_MOUNTAIN);
            reached[biome_classify(t, h, 0.0f)] = true;
        }
    for (int b = 0; b < BIOME_COUNT; b++)
        ASSERT(reached[b] == (b != BIOME_MOUNTAIN));
    return 1;
}

TEST(biome_field_deterministic)
{
    BiomeField a = biome_field_create(1234);
    BiomeField b = biome_field_create(1234);
    BiomeField c = biome_field_create(4321);
    int differs = 0;
    for (int i = 0; i < 64; i++)
    {
        float x = (float)i * 2.3f - 70.0f;
        float z = (float)i * -1.7f + 40.0f;
        BiomeSample sa = biome_field_sample(&a, x, z);
        BiomeSample sb = biome_field_sample(&b, x, z);
        BiomeSample sc = biome_field_sample(&c, x, z);
        ASSERT(memcmp(&sa, &sb, sizeof(sa)) == 0);
        ASSERT(sa.temperature >= -1.0f && sa.temperature <= 1.0f);
        ASSERT(sa.humidity >= -1.0f && sa.humidity <= 1.0f);
        differs += sa.temperature != sc.temperature || sa.humidity != sc.humidity;
    }
    ASSERT(differs > 32);
    return 1;
}

/* ---- The generated world scene world ---- */

typedef struct
{
    Scene *scene; /* The world, generated by the scene's init stages */
    VoxelVolume *vol;
    TerrainColumns cols;
    int64_t columns[BIOME_COUNT];
    /* After the heightmap (and the pillars, which the same stage raises): columns whose top
     * voxel is the biome's own surface, patch or cap, its rock (steep), a pillar's, or
     * none of these; deep lake beds can lie below the volume */
    int64_t below_volume[BIOME_COUNT];
    int64_t own_top[BIOME_COUNT];
    int64_t rock_top[BIOME_COUNT];
    int64_t pillar_top[BIOME_COUNT];
    int64_t foreign_top;
    /* After the water: lake columns under open water and under a lake cover, and water
     * voxels of another material than their column's biome water */
    int64_t open_lakes[BIOME_COUNT];
    int64_t covered_lakes[BIOME_COUNT];
    int64_t foreign_water;
    /* Trees as planned, and how many stand on their own bark once grown */
    TreePlacement *trees;
    int32_t tree_count;
    int32_t trees_on_own_bark;
    /* Voxels the shrub and flora stages placed */
    int64_t shrubs;
    int64_t flora;
    EnvParticleSystem *env;
    /* Everything generated, voxels to emitters, for comparing builds */
    uint64_t digest;
    bool tried;
    bool built;
} WorldFixture;

static WorldFixture s_world;

static int32_t top_voxel(const VoxelVolume *vol, int32_t gx, int32_t gz)
{
    for (int32_t cy = vol->chunks_y - 1; cy >= 0; cy--)
    {
        const Chunk *chunk = terrain_voxel_chunk(vol, gx, cy << CHUNK_SIZE_BITS, gz);
        if (!chunk->occupancy.has_any)
            continue;
        for (int32_t ly = CHUNK_SIZE - 1; ly >= 0; ly--)
            if (chunk_get(chunk, gx & CHUNK_SIZE_MASK, ly, gz & CHUNK_SIZE_MASK) != MAT_AIR)
                return (cy << CHUNK_SIZE_BITS) + ly;
    }
    return -1;
}

static bool is_flower_or_mushroom(uint8_t mat)
{
    return mat == MAT_FLOWER_RED || mat == MAT_FLOWER_BLUE || mat == MAT_FLOWER_YELLOW || mat == MAT_MUSHROOM ||
           mat == MAT_GLOWING_MUSHROOM;
}

static uint8_t bark_of(TreeType species)
{
    RngState rng;
    rng_seed(&rng, 1);
    return tree_config_create(species, &rng, 0.2f).bark_material;
}

#define DIGEST_START 1469598103934665603ULL

/* FNV-1a over bytes */
static uint64_t digest_bytes(uint64_t h, const void *data, size_t size)
{
    const uint8_t *b = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
    {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Voxels, each chunk's occupancy and upload state, and the volume's counters */
static uint64_t digest_volume(uint64_t h, const VoxelVolume *vol)
{
    for (int32_t c = 0; c < vol->total_chunks; c++)
    {
        const Chunk *chunk = &vol->chunks[c];
        h = digest_bytes(h, chunk->voxels, sizeof(chunk->voxels));
        h = digest_bytes(h, &chunk->occupancy.level0, sizeof(chunk->occupancy.level0));
        h = digest_bytes(h, &chunk->occupancy.level1, sizeof(chunk->occupancy.level1));
        h = digest_bytes(h, &chunk->occupancy.solid_count, sizeof(chunk->occupancy.solid_count));
        h = digest_bytes(h, &chunk->state, sizeof(chunk->state));
    }
    h = digest_bytes(h, &vol->total_solid_voxels, sizeof(vol->total_solid_voxels));
    return digest_bytes(h, &vol->active_chunks, sizeof(vol->active_chunks));
}

/* Solid voxels of the volume, from the chunks' occupancy (exact under generation writes) */
static int64_t solid_voxels(const VoxelVolume *vol)
{
    int64_t solid = 0;
    for (int32_t c = 0; c < vol->total_chunks; c++)
        solid += vol->chunks[c].occupancy.solid_count;
    return solid;
}

/* Generates the world through the world scene scene's own init stages on threads threads
 * (0: every hardware thread), recording what each stage made */
static bool world_build(WorldFixture *w, int32_t threads)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    w->scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    w->trees = (TreePlacement *)malloc(WORLD_MAX_TREES * sizeof(TreePlacement));
    if (!w->scene || !w->trees)
        return false;
    rng_seed(&w->scene->rng, 12345);
    WorldSceneData *data = (WorldSceneData *)w->scene->user_data;
    /* The scene frees its job system with it */
    data->jobs = job_system_create(threads);
    if (!data->jobs)
        return false;

    world_scene_run_init_stage(w->scene, 0); /* the volume */
    world_scene_run_init_stage(w->scene, 1); /* columns, heightmap and pillars */
    VoxelVolume *vol = data->terrain;
    const TerrainColumns *cols = &data->columns;
    if (!vol || cols->nx == 0)
        return false;
    w->vol = vol;

    for (int32_t gz = 0; gz < cols->nz; gz++)
        for (int32_t gx = 0; gx < cols->nx; gx++)
        {
            int32_t i = terrain_column_index(cols, gx, gz);
            const BiomeDesc *d = &g_biomes[cols->biome[i]];
            int32_t gy = terrain_surface_voxel(vol, cols->height[i]) - 1;
            w->columns[cols->biome[i]]++;
            if (gy < 0)
            {
                w->below_volume[cols->biome[i]]++;
                continue;
            }
            uint8_t top = terrain_voxel_get(vol, gx, gy, gz);
            if (top == cols->surface[i] && (top == d->surface || top == d->patch || top == d->cap))
                w->own_top[cols->biome[i]]++;
            else if (top == d->rock)
                w->rock_top[cols->biome[i]]++;
            else if (top >= MAT_PASTEL_FIRST && top < MAT_PASTEL_FIRST + MAT_PASTEL_COUNT)
                w->pillar_top[cols->biome[i]]++;
            else
                w->foreign_top++;
        }

    world_scene_run_init_stage(w->scene, 2); /* water */
    for (int32_t gz = 0; gz < cols->nz; gz++)
        for (int32_t gx = 0; gx < cols->nx; gx++)
        {
            int32_t i = terrain_column_index(cols, gx, gz);
            int32_t gy = terrain_surface_voxel(vol, cols->height[i]);
            uint8_t above = terrain_voxel_get(vol, gx, gy, gz);
            if (!material_is_liquid(above) && above != MAT_ICE && above != MAT_MOSS)
                continue;
            for (uint8_t m; material_is_liquid(m = terrain_voxel_get(vol, gx, gy, gz)); gy++)
                w->foreign_water += m != g_biomes[cols->biome[i]].water;
            /* Water can also end under a pillar */
            uint8_t cover = terrain_voxel_get(vol, gx, gy, gz);
            if (cover == MAT_AIR)
                w->open_lakes[cols->biome[i]]++;
            else if (cover == MAT_ICE || cover == MAT_MOSS)
                w->covered_lakes[cols->biome[i]]++;
        }

    /* The plan the tree stage grows: it plans against the volume as it is now */
    w->tree_count = terrain_plan_trees(vol, cols, data->params.tree_density, w->trees, WORLD_MAX_TREES, data->jobs);
    world_scene_run_init_stage(w->scene, 3); /* trees */
    for (int32_t t = 0; t < w->tree_count; t++)
    {
        int32_t i = terrain_column_at(cols, vol, w->trees[t].base.x, w->trees[t].base.z);
        int32_t gy = terrain_surface_voxel(vol, cols->height[i]);
        if (terrain_voxel_get(vol, i % cols->nx, gy, i / cols->nx) == bark_of(w->trees[t].species))
            w->trees_on_own_bark++;
    }

    int64_t solid = solid_voxels(vol);
    world_scene_run_init_stage(w->scene, 4); /* shrubs */
    w->shrubs = solid_voxels(vol) - solid;
    solid = solid_voxels(vol);
    world_scene_run_init_stage(w->scene, 5); /* flora; the stage frees the columns */
    w->flora = solid_voxels(vol) - solid;
    for (int32_t stage = 6; stage < WORLD_SCENE_INIT_STAGES; stage++)
        world_scene_run_init_stage(w->scene, stage);
    w->env = data->env_particles;
    /* The columns again for the checks: a pure function of the seed and the volume's size */
    const WorldSceneParams *params = &data->params;
    if (!w->env || !terrain_columns_build(&w->cols, vol, params->terrain_amplitude, params->terrain_frequency,
                                          desc->rng_seed, data->jobs))
        return false;

    uint64_t h = digest_volume(DIGEST_START, vol);
    for (int32_t t = 0; t < w->tree_count; t++)
    {
        const TreePlacement *p = &w->trees[t];
        h = digest_bytes(h, &p->base, sizeof(p->base));
        h = digest_bytes(h, &p->species, sizeof(p->species));
        h = digest_bytes(h, &p->shape_seed, sizeof(p->shape_seed));
    }
    const int64_t counts[] = {w->tree_count, w->shrubs, w->flora, w->env->emitter_count};
    h = digest_bytes(h, counts, sizeof(counts));
    for (int32_t e = 0; e < w->env->emitter_count; e++)
    {
        const EnvEmitter *em = &w->env->emitters[e];
        h = digest_bytes(h, &em->position, sizeof(em->position));
        h = digest_bytes(h, &em->type, sizeof(em->type));
        h = digest_bytes(h, &em->source_material, sizeof(em->source_material));
    }
    w->digest = h;
    return true;
}

static void world_release(WorldFixture *w)
{
    terrain_columns_release(&w->cols);
    free(w->trees);
    scene_destroy(w->scene);
    memset(w, 0, sizeof(*w));
}

static const WorldFixture *world(void)
{
    if (!s_world.tried)
    {
        s_world.tried = true;
        s_world.built = world_build(&s_world, 0);
    }
    return s_world.built ? &s_world : NULL;
}

TEST(world_has_every_biome)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    const int64_t total = (int64_t)w->cols.nx * w->cols.nz;
    /* Each biome covers at least 1% of the map (a patch of ~15 x 15 m) */
    for (int b = 0; b < BIOME_COUNT; b++)
        ASSERT(w->columns[b] * 100 >= total);
    return 1;
}

TEST(world_ground_matches_biome)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    /* Every column shows its biome's own top material, or its rock on a cliff (unless a
     * pillar stands on it) */
    ASSERT_EQ(w->foreign_top, 0);
    for (int b = 0; b < BIOME_COUNT; b++)
    {
        ASSERT(w->own_top[b] + w->rock_top[b] + w->pillar_top[b] + w->below_volume[b] == w->columns[b]);
        /* Cliffs are the exception, not the look */
        ASSERT(w->own_top[b] * 10 >= w->columns[b] * 7);
    }
    return 1;
}

TEST(world_lakes_follow_biome)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    for (int b = 0; b < BIOME_COUNT; b++)
    {
        const BiomeDesc *d = &g_biomes[b];
        if (d->lake_cover == MAT_AIR)
            ASSERT_EQ(w->covered_lakes[b], 0);
        else if (d->lake_cover_threshold <= -1.0f)
            ASSERT_EQ(w->open_lakes[b], 0);
        else
            ASSERT(w->covered_lakes[b] > 0 && w->open_lakes[b] > 0);
    }
    /* Every lake holds its biome's water, the swamp's murky */
    ASSERT_EQ(w->foreign_water, 0);
    ASSERT(g_biomes[BIOME_SWAMP].water != g_biomes[BIOME_GRASSLAND].water);
    /* The snow biome's lakes (frozen over) are part of this world */
    ASSERT(w->covered_lakes[BIOME_SNOW] > 0);
    return 1;
}

TEST(world_trees_follow_biome)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    ASSERT(w->tree_count > 0 && w->tree_count < WORLD_MAX_TREES);

    int32_t per_biome[BIOME_COUNT] = {0};
    for (int32_t t = 0; t < w->tree_count; t++)
    {
        const TreePlacement *p = &w->trees[t];
        int32_t i = terrain_column_at(&w->cols, w->vol, p->base.x, p->base.z);
        ASSERT_EQ(p->base.y, w->cols.height[i]);

        /* The species is one the biome at its base lists */
        const uint8_t biome = w->cols.biome[i];
        bool listed = false;
        for (int s = 0; s < BIOME_MAX_TREES; s++)
            listed |= g_biomes[biome].trees[s].weight > 0.0f && g_biomes[biome].trees[s].species == p->species;
        ASSERT(listed);
        per_biome[biome]++;
    }

    /* Forested biomes are forested; open ones stay far sparser per area */
    for (int b = 0; b < BIOME_COUNT; b++)
        if (g_biomes[b].tree_chance >= 0.5f)
            ASSERT(per_biome[b] >= 10);
    ASSERT((float)per_biome[BIOME_FOREST] / (float)w->columns[BIOME_FOREST] >
           2.0f * (float)per_biome[BIOME_GRASSLAND] / (float)w->columns[BIOME_GRASSLAND]);

    /* Each planned tree grew: its trunk stands on its base */
    ASSERT(w->trees_on_own_bark * 100 >= w->tree_count * 95);
    return 1;
}

TEST(world_flora_follows_biome)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    ASSERT(w->shrubs > 0);
    ASSERT(w->flora > 0);

    /* Flowers and mushrooms come only from the scatter: each sits right on the terrain,
     * under the open sky or under a canopy, never on water or a lake cover, on a ground its
     * column's biome lists it for */
    const int32_t total_y = w->vol->chunks_y * CHUNK_SIZE;
    int64_t placed[BIOME_COUNT] = {0};
    int64_t under_canopy = 0;
    for (int32_t gz = 0; gz < w->cols.nz; gz++)
        for (int32_t gx = 0; gx < w->cols.nx; gx++)
        {
            const int32_t i = terrain_column_index(&w->cols, gx, gz);
            const int32_t gy = terrain_surface_voxel(w->vol, w->cols.height[i]);
            const int32_t top = top_voxel(w->vol, gx, gz);
            if (top > 0 && is_flower_or_mushroom(terrain_voxel_get(w->vol, gx, top, gz)))
                ASSERT_EQ(top, gy);
            if (gy <= 0 || gy >= total_y)
                continue;
            uint8_t mat = terrain_voxel_get(w->vol, gx, gy, gz);
            if (!is_flower_or_mushroom(mat))
                continue;
            under_canopy += top != gy;
            uint8_t below = terrain_voxel_get(w->vol, gx, gy - 1, gz);
            uint8_t biome = w->cols.biome[i];
            bool listed = false;
            for (int f = 0; f < BIOME_MAX_FLORA; f++)
            {
                const BiomeFlora *flora = &g_biomes[biome].flora[f];
                listed |= flora->density > 0.0f && flora->material == mat && flora->on_material == below;
            }
            ASSERT(listed);
            placed[biome]++;
        }
    printf("(%lld under a canopy) ", (long long)under_canopy);
    ASSERT(under_canopy > 0);
    /* Every biome with flowers or mushrooms in its table got some */
    for (int b = 0; b < BIOME_COUNT; b++)
    {
        bool has = false;
        for (int f = 0; f < BIOME_MAX_FLORA; f++)
            has |= g_biomes[b].flora[f].density > 0.0f && is_flower_or_mushroom(g_biomes[b].flora[f].material);
        if (has)
            ASSERT(placed[b] > 0);
    }
    return 1;
}

/* Longest run of consecutive voxel rows along which a biome border stays on one voxel line */
static int32_t longest_straight_border(const TerrainColumns *cols)
{
    int32_t longest = 0;
    int32_t *run = (int32_t *)calloc((size_t)(cols->nx > cols->nz ? cols->nx : cols->nz), sizeof(int32_t));
    if (!run)
        return INT32_MAX;
    for (int32_t gz = 0; gz < cols->nz; gz++)
        for (int32_t gx = 0; gx + 1 < cols->nx; gx++)
        {
            int32_t i = terrain_column_index(cols, gx, gz);
            run[gx] = cols->biome[i] != cols->biome[i + 1] ? run[gx] + 1 : 0;
            longest = run[gx] > longest ? run[gx] : longest;
        }
    memset(run, 0, sizeof(int32_t) * (size_t)(cols->nx > cols->nz ? cols->nx : cols->nz));
    for (int32_t gx = 0; gx < cols->nx; gx++)
        for (int32_t gz = 0; gz + 1 < cols->nz; gz++)
        {
            int32_t i = terrain_column_index(cols, gx, gz);
            run[gz] = cols->biome[i] != cols->biome[i + cols->nx] ? run[gz] + 1 : 0;
            longest = run[gz] > longest ? run[gz] : longest;
        }
    free(run);
    return longest;
}

TEST(world_borders_not_straight)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    /* Warped and dithered borders never follow a voxel line for 2 m (20 voxels); the
     * unwarped noise lattice drew borders straight for up to 8.7 m */
    int32_t longest = longest_straight_border(&w->cols);
    printf("(longest straight border %d voxels) ", longest);
    ASSERT(longest < 20);
    return 1;
}

static uint64_t hash_volume(const VoxelVolume *vol)
{
    uint64_t h = 1469598103934665603ULL;
    for (int32_t c = 0; c < vol->total_chunks; c++)
        for (int32_t v = 0; v < CHUNK_VOXEL_COUNT; v++)
        {
            h ^= vol->chunks[c].voxels[v].material;
            h *= 1099511628211ULL;
        }
    return h;
}

/* A small world through every generation stage */
static uint64_t generate_small_world(uint32_t seed)
{
    VoxelVolume *vol = volume_create_dims(8, 4, 8, vec3_create(-12.8f, 0.0f, -12.8f), 0.1f);
    if (!vol)
        return 0;
    TerrainColumns cols;
    uint64_t h = 0;
    if (terrain_columns_build(&cols, vol, 8.0f, 0.06f, seed, NULL))
    {
        terrain_gen_heightmap(vol, &cols, NULL);
        terrain_gen_pillars(vol, 4, 8.0f, 0.06f, seed);
        terrain_gen_water(vol, &cols, NULL);
        terrain_gen_trees(vol, &cols, 1.0f, NULL);
        shrub_gen_place(vol, &cols);
        scatter_gen_apply(vol, &cols, NULL);
        h = hash_volume(vol);
        terrain_columns_release(&cols);
    }
    volume_destroy(vol);
    return h;
}

TEST(worldgen_deterministic)
{
    uint64_t a = generate_small_world(77);
    uint64_t b = generate_small_world(77);
    uint64_t c = generate_small_world(78);
    ASSERT(a != 0);
    ASSERT_EQ(a, b);
    ASSERT(a != c);
    return 1;
}

TEST(worldgen_same_for_any_thread_count)
{
    /* The world, its trees, flora and emitters come out identical whether one thread
     * generates it or several split the stages between them */
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    const int32_t threads[] = {1, 2, 3};
    printf("(%d hardware threads) ", platform_thread_hardware_count());
    for (size_t i = 0; i < sizeof(threads) / sizeof(threads[0]); i++)
    {
        WorldFixture other = {0};
        bool built = world_build(&other, threads[i]);
        uint64_t digest = other.digest;
        bool same_stats = other.tree_count == w->tree_count && other.shrubs == w->shrubs && other.flora == w->flora &&
                          memcmp(other.columns, w->columns, sizeof(w->columns)) == 0;
        world_release(&other);
        ASSERT(built);
        ASSERT(same_stats);
        ASSERT_EQ(digest, w->digest);
    }
    return 1;
}

TEST(world_occupancy_exact_after_generation)
{
    /* Generation writes keep each chunk's occupancy exact, so counting the solid voxels
     * from it (volume_recount_solid) matches a full rescan of every voxel */
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    Chunk *scratch = (Chunk *)malloc(sizeof(Chunk));
    ASSERT(scratch != NULL);
    int64_t solid = 0;
    int32_t active = 0;
    bool same = true;
    for (int32_t c = 0; c < w->vol->total_chunks && same; c++)
    {
        const Chunk *chunk = &w->vol->chunks[c];
        memcpy(scratch, chunk, sizeof(Chunk));
        chunk_rebuild_occupancy(scratch);
        same = scratch->occupancy.level0 == chunk->occupancy.level0 &&
               scratch->occupancy.level1 == chunk->occupancy.level1 &&
               scratch->occupancy.solid_count == chunk->occupancy.solid_count &&
               scratch->occupancy.has_any == chunk->occupancy.has_any;
        solid += scratch->occupancy.solid_count;
        active += scratch->occupancy.has_any;
    }
    free(scratch);
    ASSERT(same);
    ASSERT_EQ(solid, (int64_t)w->vol->total_solid_voxels);
    ASSERT_EQ(active, w->vol->active_chunks);
    return 1;
}

/* Every inner column holds max(terrain, lowest neighbouring level) and every edge column
 * max(terrain, sea level): the levels a flood relaxing from above settles at. Relaxation
 * never takes a level below that fixed point, so meeting it means the flood reached it. */
static bool flood_levels_settled(const float *terrain, const float *water, int32_t nx, int32_t nz, float sea)
{
    for (int32_t gz = 0; gz < nz; gz++)
        for (int32_t gx = 0; gx < nx; gx++)
        {
            int32_t i = gz * nx + gx;
            float expect;
            if (gx == 0 || gx == nx - 1 || gz == 0 || gz == nz - 1)
                expect = fmaxf(terrain[i], sea);
            else
                expect = fmaxf(terrain[i], fminf(fminf(water[i - 1], water[i + 1]), fminf(water[i - nx], water[i + nx])));
            if (water[i] != expect)
                return false;
        }
    return true;
}

TEST(flood_settles_on_world_map)
{
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    const int32_t n = w->cols.nx * w->cols.nz;
    float *water = (float *)malloc((size_t)n * sizeof(float));
    JobSystem *jobs = job_system_create(0);
    bool flooded = water && jobs &&
                   terrain_flood_water_levels(w->cols.height, water, w->cols.nx, w->cols.nz, TERRAIN_SEA_LEVEL, jobs);
    bool settled = flooded && flood_levels_settled(w->cols.height, water, w->cols.nx, w->cols.nz, TERRAIN_SEA_LEVEL);
    int32_t lakes = 0;
    for (int32_t i = 0; flooded && i < n; i++)
        lakes += water[i] > w->cols.height[i];
    job_system_destroy(jobs);
    free(water);
    ASSERT(flooded);
    ASSERT(settled);
    ASSERT(lakes > 0);
    return 1;
}

TEST(flood_drains_serpentine_channel_across_blocks)
{
    /* A one-column channel snaking over a map many flood blocks wide from its exit on the
     * far edge: the drain level has to cross block borders dozens of times, much of it
     * against a raster scan's direction. No channel column may keep water. */
    enum { N = 300, ROW_SPACING = 3 };
    const float wall = 10.0f;
    float *terrain = (float *)malloc(N * N * sizeof(float));
    float *water = (float *)malloc(N * N * sizeof(float));
    JobSystem *jobs = job_system_create(0);
    if (!terrain || !water || !jobs)
    {
        free(terrain);
        free(water);
        job_system_destroy(jobs);
        ASSERT(false);
    }
    for (int i = 0; i < N * N; i++)
        terrain[i] = wall;
    terrain[(N - 1) * N + (N - 2)] = 0.0f; /* exit on the +z edge */
    terrain[(N - 2) * N + (N - 2)] = 0.0f;
    int row = N - 3;
    for (int k = 0; row >= 1; k++, row -= ROW_SPACING)
    {
        for (int gx = 1; gx <= N - 2; gx++)
            terrain[row * N + gx] = 0.0f;
        int connector_x = (k % 2 == 0) ? 1 : N - 2;
        for (int gz = row - ROW_SPACING; gz < row && gz >= 1; gz++)
            terrain[gz * N + connector_x] = 0.0f;
    }

    bool flooded = terrain_flood_water_levels(terrain, water, N, N, -1.0f, jobs);
    bool drained = true;
    for (int i = 0; flooded && i < N * N; i++)
        drained &= water[i] == terrain[i];
    job_system_destroy(jobs);
    free(terrain);
    free(water);
    ASSERT(flooded);
    ASSERT(drained);
    return 1;
}

/* The particle types a biome can show: those of every top voxel it can have (ground,
 * flora, leaves of its trees and shrubs, open water) */
static uint32_t biome_particle_types(BiomeKind b)
{
    const BiomeDesc *d = &g_biomes[b];
    uint8_t tops[32];
    int32_t n = 0;
    tops[n++] = d->surface;
    tops[n++] = d->patch;
    tops[n++] = d->cap;
    tops[n++] = d->rock;
    tops[n++] = d->lake_cover == MAT_AIR || d->lake_cover_threshold > -1.0f ? d->water : MAT_AIR;
    for (int i = 0; i < BIOME_MAX_FLORA; i++)
        tops[n++] = d->flora[i].density > 0.0f ? d->flora[i].material : MAT_AIR;
    for (int i = 0; i < BIOME_MAX_SHRUBS; i++)
        tops[n++] = d->shrubs[i].weight > 0.0f ? d->shrubs[i].material : MAT_AIR;
    for (int i = 0; i < BIOME_MAX_TREES; i++)
    {
        if (d->trees[i].weight <= 0.0f)
            continue;
        RngState rng;
        rng_seed(&rng, 1);
        tops[n++] = tree_config_create(d->trees[i].species, &rng, 0.2f).leaf_material;
    }

    uint32_t types = 0;
    for (int32_t t = 0; t < n; t++)
        for (int32_t r = 0; r < g_env_emitter_rule_count; r++)
            if (tops[t] != MAT_AIR && g_env_emitter_rules[r].material == tops[t])
                types |= 1u << g_env_emitter_rules[r].type;
    return types;
}

TEST(emitters_cover_world_and_types)
{
    /* Emitters spread over the whole world instead of filling up along one edge, and
     * every particle type a biome supports gets some */
    const WorldFixture *w = world();
    ASSERT(w != NULL);
    const EnvParticleSystem *env = w->env;
    ASSERT(env->emitter_count > ENV_EMITTER_MAX / 2);

    const Bounds3D *b = &w->vol->bounds;
    enum { TILES = 4 };
    int32_t per_tile[TILES * TILES] = {0};
    uint32_t types_found = 0;
    float min_x = FLT_MAX, max_x = -FLT_MAX, min_z = FLT_MAX, max_z = -FLT_MAX;
    for (int32_t e = 0; e < env->emitter_count; e++)
    {
        Vec3 p = env->emitters[e].position;
        min_x = fminf(min_x, p.x);
        max_x = fmaxf(max_x, p.x);
        min_z = fminf(min_z, p.z);
        max_z = fmaxf(max_z, p.z);
        int32_t tx = (int32_t)((p.x - b->min_x) / (b->max_x - b->min_x) * TILES);
        int32_t tz = (int32_t)((p.z - b->min_z) / (b->max_z - b->min_z) * TILES);
        ASSERT(tx >= 0 && tx < TILES && tz >= 0 && tz < TILES);
        per_tile[tz * TILES + tx]++;
        types_found |= 1u << env->emitters[e].type;
    }
    /* The extremes reach within 5% of each edge, and no sixteenth of the map is left
     * with less than half a percent of the emitters (an even spread gives 6%) */
    const float span_x = b->max_x - b->min_x, span_z = b->max_z - b->min_z;
    ASSERT(min_x < b->min_x + 0.05f * span_x && max_x > b->max_x - 0.05f * span_x);
    ASSERT(min_z < b->min_z + 0.05f * span_z && max_z > b->max_z - 0.05f * span_z);
    for (int32_t t = 0; t < TILES * TILES; t++)
        ASSERT(per_tile[t] * 200 >= env->emitter_count);

    uint32_t supported = 0;
    for (int bk = 0; bk < BIOME_COUNT; bk++)
        supported |= biome_particle_types((BiomeKind)bk);
    printf("(types supported %02x found %02x) ", supported, types_found);
    ASSERT(supported != 0);
    ASSERT((types_found & supported) == supported);
    return 1;
}

int main(void)
{
    printf("=== World Generation Tests ===\n");

    RUN_TEST(biome_surfaces_distinct);
    RUN_TEST(biome_ground_tables_valid);
    RUN_TEST(biome_tree_tables_valid);
    RUN_TEST(biome_flora_tables_valid);
    RUN_TEST(biome_climate_reaches_every_biome);
    RUN_TEST(biome_field_deterministic);
    RUN_TEST(worldgen_deterministic);
    RUN_TEST(world_has_every_biome);
    RUN_TEST(world_ground_matches_biome);
    RUN_TEST(world_lakes_follow_biome);
    RUN_TEST(world_trees_follow_biome);
    RUN_TEST(world_flora_follows_biome);
    RUN_TEST(world_borders_not_straight);
    RUN_TEST(world_occupancy_exact_after_generation);
    RUN_TEST(flood_settles_on_world_map);
    RUN_TEST(flood_drains_serpentine_channel_across_blocks);
    RUN_TEST(emitters_cover_world_and_types);
    RUN_TEST(worldgen_same_for_any_thread_count);

    world_release(&s_world);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
