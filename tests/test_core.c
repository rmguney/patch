#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/noise.h"
#include "engine/core/rng.h"
#include "engine/core/spatial_hash.h"
#include "test_common.h"
#include <math.h>
#include <stdlib.h>

TEST(rng_golden_sequence)
{
    /* xorshift64 (13, 7, 17): any change to the generator or the seeding changes every
     * seeded world, so the stream is pinned */
    static const uint64_t expected[5] = {0x32db5e49f304284bULL, 0xd0720d2b815752dbULL, 0x3702004f3ca62abeULL,
                                         0x97cd6c105cd4096bULL, 0xc8b069c0ff309bb9ULL};
    RngState rng;
    rng_seed(&rng, 0xCAFEBABE);
    for (int i = 0; i < 5; i++)
        ASSERT(rng_next(&rng) == expected[i]);

    rng_seed(&rng, 0x12345678);
    for (int i = 0; i < 1000; i++)
        rng_next(&rng);
    ASSERT(rng.state == 0x127af83da1049fe4ULL);

    /* Seed 0 would stay 0 forever */
    rng_seed(&rng, 0);
    ASSERT(rng_next(&rng) != 0);
    return 1;
}

TEST(rng_different_seeds)
{
    RngState rng1, rng2;
    rng_seed(&rng1, 0x12345678);
    rng_seed(&rng2, 0x87654321);

    int same_count = 0;
    for (int i = 0; i < 100; i++)
    {
        if (rng_next(&rng1) == rng_next(&rng2))
            same_count++;
    }
    ASSERT(same_count < 5);
    return 1;
}

TEST(rng_range_bounds)
{
    RngState rng;
    rng_seed(&rng, 0xDEADBEEF);

    for (int i = 0; i < 1000; i++)
    {
        float f = rng_range_f32(&rng, 1.0f, 5.0f);
        ASSERT(f >= 1.0f && f <= 5.0f);

        uint32_t u = rng_range_u32(&rng, 10);
        ASSERT(u < 10);
    }
    return 1;
}

TEST(math_clamp)
{
    ASSERT(clampf(5.0f, 0.0f, 10.0f) == 5.0f);
    ASSERT(clampf(-5.0f, 0.0f, 10.0f) == 0.0f);
    ASSERT(clampf(15.0f, 0.0f, 10.0f) == 10.0f);
    return 1;
}

TEST(math_lerp)
{
    ASSERT(fabsf(lerpf(0.0f, 10.0f, 0.5f) - 5.0f) < 0.0001f);
    ASSERT(fabsf(lerpf(0.0f, 10.0f, 0.0f) - 0.0f) < 0.0001f);
    ASSERT(fabsf(lerpf(0.0f, 10.0f, 1.0f) - 10.0f) < 0.0001f);
    return 1;
}

TEST(quat_nlerp_takes_the_short_arc)
{
    /* A quarter turn about y, halfway: an eighth turn; and q and -q (the same rotation)
     * blend without a detour through the long way round */
    const float h = 0.70710678f;
    Quat a = quat_identity();
    Quat b = quat_create(0.0f, h, 0.0f, h);
    Quat mid = quat_nlerp(a, b, 0.5f);
    Vec3 x = quat_rotate_vec3(mid, vec3_create(1.0f, 0.0f, 0.0f));
    ASSERT_NEAR(x.x, h, 1e-5f);
    ASSERT_NEAR(x.y, 0.0f, 1e-5f);
    ASSERT_NEAR(fabsf(x.z), h, 1e-5f);
    Quat neg_b = quat_create(-b.x, -b.y, -b.z, -b.w);
    Vec3 y = quat_rotate_vec3(quat_nlerp(a, neg_b, 0.5f), vec3_create(1.0f, 0.0f, 0.0f));
    ASSERT_NEAR(y.x, x.x, 1e-5f);
    ASSERT_NEAR(y.z, x.z, 1e-5f);
    Quat end = quat_nlerp(a, b, 1.0f);
    ASSERT_NEAR(end.y, b.y, 1e-6f);
    ASSERT_NEAR(end.w, b.w, 1e-6f);
    return 1;
}

TEST(vec3_operations)
{
    Vec3 a = vec3_create(1.0f, 2.0f, 3.0f);
    Vec3 b = vec3_create(4.0f, 5.0f, 6.0f);

    Vec3 sum = vec3_add(a, b);
    ASSERT(fabsf(sum.x - 5.0f) < 0.0001f);
    ASSERT(fabsf(sum.y - 7.0f) < 0.0001f);
    ASSERT(fabsf(sum.z - 9.0f) < 0.0001f);

    Vec3 diff = vec3_sub(b, a);
    ASSERT(fabsf(diff.x - 3.0f) < 0.0001f);
    ASSERT(fabsf(diff.y - 3.0f) < 0.0001f);
    ASSERT(fabsf(diff.z - 3.0f) < 0.0001f);

    float dot = vec3_dot(a, b);
    ASSERT(fabsf(dot - 32.0f) < 0.0001f);

    return 1;
}

/* ------------------------------------------------------------------------- */
/* Spatial hash                                                              */
/* ------------------------------------------------------------------------- */

static const Bounds3D HASH_BOUNDS = {-8.0f, 8.0f, 0.0f, 8.0f, -8.0f, 8.0f};

static bool contains(const int32_t *list, int32_t count, int32_t value)
{
    for (int32_t i = 0; i < count; i++)
        if (list[i] == value)
            return true;
    return false;
}

TEST(spatial_hash_finds_every_overlapping_object_once)
{
    SpatialHashGrid *grid = (SpatialHashGrid *)malloc(sizeof(SpatialHashGrid));
    ASSERT(grid != NULL);
    spatial_hash_init(grid, 0.25f, HASH_BOUNDS);

    /* Objects on a 0.1 m lattice, some spanning several cells */
    RngState rng;
    rng_seed(&rng, 11);
    enum { OBJECTS = 400 };
    Vec3 pos[OBJECTS];
    float radius[OBJECTS];
    for (int32_t i = 0; i < OBJECTS; i++)
    {
        pos[i] = vec3_create(rng_range_f32(&rng, -2.0f, 2.0f), rng_range_f32(&rng, 0.5f, 2.5f), rng_range_f32(&rng, -2.0f, 2.0f));
        radius[i] = (i % 10 == 0) ? 0.4f : 0.03f;
        spatial_hash_insert(grid, i, pos[i], radius[i]);
    }

    /* Every object whose box overlaps the query box is reported, and exactly once */
    for (int32_t q = 0; q < 50; q++)
    {
        Vec3 center = vec3_create(rng_range_f32(&rng, -2.0f, 2.0f), rng_range_f32(&rng, 0.5f, 2.5f), rng_range_f32(&rng, -2.0f, 2.0f));
        float r = 0.3f;
        int32_t found[OBJECTS];
        int32_t count = spatial_hash_query(grid, center, r, found, OBJECTS);

        for (int32_t i = 0; i < count; i++)
            for (int32_t j = i + 1; j < count; j++)
                ASSERT(found[i] != found[j]);

        for (int32_t i = 0; i < OBJECTS; i++)
        {
            float reach = r + radius[i];
            bool overlaps = fabsf(pos[i].x - center.x) <= reach && fabsf(pos[i].y - center.y) <= reach &&
                            fabsf(pos[i].z - center.z) <= reach;
            if (overlaps)
                ASSERT(contains(found, count, i));
        }
    }

    spatial_hash_clear(grid);
    int32_t found[4];
    ASSERT(spatial_hash_query(grid, pos[0], 1.0f, found, 4) == 0);

    free(grid);
    return 1;
}

TEST(spatial_hash_respects_limits)
{
    SpatialHashGrid *grid = (SpatialHashGrid *)malloc(sizeof(SpatialHashGrid));
    ASSERT(grid != NULL);
    spatial_hash_init(grid, 0.25f, HASH_BOUNDS);

    /* max_results caps the answer */
    for (int32_t i = 0; i < 10; i++)
        spatial_hash_insert(grid, i, vec3_create(0.1f, 1.0f, 0.1f), 0.01f);
    int32_t found[10];
    ASSERT(spatial_hash_query(grid, vec3_create(0.1f, 1.0f, 0.1f), 0.1f, found, 3) == 3);

    /* An object covering more cells than the table holds entries for fills it and stops */
    spatial_hash_clear(grid);
    spatial_hash_insert(grid, 0, vec3_create(0.0f, 4.0f, 0.0f), 8.0f); /* 65^3 cells */
    ASSERT(grid->entry_count == SPATIAL_HASH_MAX_ENTRIES);

    /* Queries keep working across the de-duplication generation wrapping around */
    spatial_hash_clear(grid);
    spatial_hash_insert(grid, 5, vec3_create(1.0f, 1.0f, 1.0f), 0.3f);
    grid->query_generation = UINT32_MAX - 1;
    for (int32_t q = 0; q < 4; q++)
    {
        ASSERT(spatial_hash_query(grid, vec3_create(1.0f, 1.0f, 1.0f), 0.3f, found, 10) == 1);
        ASSERT(found[0] == 5);
    }

    free(grid);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Noise                                                                     */
/* ------------------------------------------------------------------------- */

TEST(noise_value_range)
{
    float lo = 1.0f, hi = -1.0f;
    for (int32_t i = 0; i < 20000; i++)
    {
        float x = (float)(i % 200) * 0.173f - 17.0f;
        float z = (float)(i / 200) * 0.291f - 13.0f;
        float n = noise_value_2d(x, z, 0x2468ACE0u);
        ASSERT(n >= -1.0f && n <= 1.0f);
        lo = minf(lo, n);
        hi = maxf(hi, n);

        /* fBm stays within the sum of its octave amplitudes, ridged within [0, that] */
        float amp_sum = 1.0f + 0.5f + 0.25f + 0.125f;
        float fbm = noise_fbm_2d(x, z, 7u, 4, 2.0f, 0.5f);
        ASSERT(fabsf(fbm) <= amp_sum);
        float ridged = noise_ridged_2d(x, z, 7u, 4, 2.0f, 0.5f);
        ASSERT(ridged >= 0.0f && ridged <= amp_sum);
    }
    /* And it uses the range */
    ASSERT(lo < -0.8f && hi > 0.8f);
    return 1;
}

TEST(noise_continuous)
{
    /* Smoothstep interpolation of lattice values in [-1, 1]: the slope is at most
     * 2 * 1.5 per unit, also across lattice lines */
    const float step = 1.0f / 1024.0f;
    for (int32_t i = 0; i < 4000; i++)
    {
        float x = -3.0f + (float)i * step * 3.0f;
        float z = 0.37f + (float)(i % 7);
        float a = noise_value_2d(x, z, 99u);
        ASSERT(fabsf(noise_value_2d(x + step, z, 99u) - a) <= 3.0f * step + 1e-5f);
        ASSERT(fabsf(noise_value_2d(x, z + step, 99u) - a) <= 3.0f * step + 1e-5f);
    }
    return 1;
}

/* Value noise at a lattice point is that point's hash value, so lattice samples test
 * the hash directly */
#define LATTICE_PROBES 32

static void lattice_probe(int32_t k, int32_t *x, int32_t *z)
{
    *x = (k * 37) % 101 - 50;
    *z = (k * 53) % 89 - 44;
}

static void lattice_probe_values(uint32_t seed, float *values)
{
    for (int32_t k = 0; k < LATTICE_PROBES; k++)
    {
        int32_t x, z;
        lattice_probe(k, &x, &z);
        values[k] = noise_value_2d((float)x, (float)z, seed);
    }
}

/* How many probes p see the value of p under the probed seed at p + (dx, dz) under seed */
static int32_t shifted_matches(const float *probed, uint32_t seed, int32_t dx, int32_t dz)
{
    int32_t matches = 0;
    for (int32_t k = 0; k < LATTICE_PROBES; k++)
    {
        int32_t x, z;
        lattice_probe(k, &x, &z);
        matches += noise_value_2d((float)(x + dx), (float)(z + dz), seed) == probed[k];
    }
    return matches;
}

TEST(noise_no_lattice_translation_symmetry)
{
    /* A hash of x + 57z made the noise constant along (57, -1): an octave repeated every
     * 57 lattice cells (119 m for the terrain's fourth octave, inside the 153 m map).
     * Two lattice values agree by chance with probability 2^-24, so no translation may
     * map more than a couple of the probes onto equal values. */
    const uint32_t seed = 0x12345678u;
    float probed[LATTICE_PROBES];
    lattice_probe_values(seed, probed);
    for (int32_t dz = -160; dz <= 160; dz++)
        for (int32_t dx = -160; dx <= 160; dx++)
            if (dx != 0 || dz != 0)
                ASSERT(shifted_matches(probed, seed, dx, dz) <= 2);
    return 1;
}

TEST(noise_seed_independence)
{
    /* Neighbouring seeds, and the offset seeds generators use for separate fields and
     * octaves (+1000, +2000...), must be unrelated noises: neither a shifted copy of each
     * other (seed + 1 was seed shifted 131 cells in x) nor correlated where they overlap */
    const uint32_t seed = 0x12345678u;
    const uint32_t offsets[] = {1u, 2u, 131u, 1000u, 2000u, 3000u};
    float probed[LATTICE_PROBES];
    lattice_probe_values(seed, probed);
    for (size_t o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++)
    {
        const uint32_t other = seed + offsets[o];
        for (int32_t dz = -16; dz <= 16; dz++)
            for (int32_t dx = -400; dx <= 400; dx++)
                ASSERT(shifted_matches(probed, other, dx, dz) <= 2);

        /* Pearson correlation over continuous samples */
        double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
        const int32_t n = 8192;
        for (int32_t i = 0; i < n; i++)
        {
            const int32_t column = i % 128;
            const int32_t row = i / 128;
            float x = (float)column * 0.71f - 40.0f;
            float z = (float)row * 0.67f - 20.0f;
            double a = noise_value_2d(x, z, seed);
            double b = noise_value_2d(x, z, other);
            sa += a;
            sb += b;
            saa += a * a;
            sbb += b * b;
            sab += a * b;
        }
        double cov = sab / n - (sa / n) * (sb / n);
        double r = cov / sqrt((saa / n - (sa / n) * (sa / n)) * (sbb / n - (sb / n) * (sb / n)));
        ASSERT(fabs(r) < 0.06);
    }
    return 1;
}

TEST(noise_lattice_values_uniform)
{
    /* The hash spreads lattice values evenly over [-1, 1): each of 16 equal bins gets its
     * share of 64k lattice points within 5% */
    int32_t bins[16] = {0};
    for (int32_t z = 0; z < 256; z++)
        for (int32_t x = 0; x < 256; x++)
        {
            float v = noise_value_2d((float)(x - 128), (float)(z - 128), 42u);
            ASSERT(v >= -1.0f && v < 1.0f);
            bins[(int32_t)((v + 1.0f) * 8.0f)]++;
        }
    for (int32_t b = 0; b < 16; b++)
        ASSERT(abs(bins[b] - 4096) < 205);
    return 1;
}

TEST(noise_fbm_octaves_not_aligned)
{
    /* Value noise is flat at its lattice points. With lacunarity 2 and unshifted octaves
     * every octave has a lattice point on each lattice point of the first, so the sum
     * was flat on that grid; shifted octaves keep the slope there like anywhere else. */
    const float e = 1.0f / 512.0f;
    double slope_lattice = 0.0, slope_between = 0.0;
    for (int32_t i = 0; i < 1024; i++)
    {
        const int32_t column = i % 32;
        const int32_t row = i / 32;
        float x = (float)(column - 16);
        float z = (float)(row - 16);
        for (int32_t pass = 0; pass < 2; pass++)
        {
            float px = x + (pass ? 0.37f : 0.0f);
            float pz = z + (pass ? 0.61f : 0.0f);
            double slope = fabsf(noise_fbm_2d(px + e, pz, 5u, 6, 2.0f, 0.5f) - noise_fbm_2d(px - e, pz, 5u, 6, 2.0f, 0.5f)) +
                           fabsf(noise_fbm_2d(px, pz + e, 5u, 6, 2.0f, 0.5f) - noise_fbm_2d(px, pz - e, 5u, 6, 2.0f, 0.5f));
            if (pass)
                slope_between += slope;
            else
                slope_lattice += slope;
        }
    }
    ASSERT(slope_lattice > 0.5 * slope_between);
    return 1;
}

int main(void)
{
    printf("=== Core Tests ===\n");

    RUN_TEST(rng_golden_sequence);
    RUN_TEST(rng_different_seeds);
    RUN_TEST(rng_range_bounds);
    RUN_TEST(math_clamp);
    RUN_TEST(math_lerp);
    RUN_TEST(quat_nlerp_takes_the_short_arc);
    RUN_TEST(vec3_operations);

    printf("\n=== Spatial Hash Tests ===\n");
    RUN_TEST(spatial_hash_finds_every_overlapping_object_once);
    RUN_TEST(spatial_hash_respects_limits);

    printf("\n=== Noise Tests ===\n");
    RUN_TEST(noise_value_range);
    RUN_TEST(noise_continuous);
    RUN_TEST(noise_no_lattice_translation_symmetry);
    RUN_TEST(noise_seed_independence);
    RUN_TEST(noise_lattice_values_uniform);
    RUN_TEST(noise_fbm_octaves_not_aligned);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
