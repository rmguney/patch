#include "engine/core/noise.h"
#include <math.h>

/* Lattice values hash (seed, x, z) without the linear combination the old hash made of
 * them (x + 57z + 131 seed: the noise repeated along (57, -1), and seed s + 1 was seed s
 * moved 131 cells). The seed and x go through a murmur3 block step (multiply, rotate,
 * multiply, fold, rotate); z then advances that row state as a Weyl sequence and the
 * murmur3 finalizer avalanches the sum, so every input bit reaches every output bit and
 * no translation or seed change maps the lattice onto itself. The two row states are
 * shared by the four corners of a cell. */
#define MURMUR_C1 0xCC9E2D51u
#define MURMUR_C2 0x1B873593u
#define WEYL_STEP 0x9E3779B9u /* 2^32 / golden ratio, odd */

static inline uint32_t rotl32(uint32_t v, int r)
{
    return (v << r) | (v >> (32 - r));
}

static inline uint32_t lattice_row(uint32_t seed, int32_t x)
{
    uint32_t k = (uint32_t)x * MURMUR_C1;
    k = rotl32(k, 15) * MURMUR_C2;
    return rotl32(seed ^ k, 13) * 5u + 0xE6546B64u;
}

/* murmur3's finalizer: each input bit flips each output bit with probability ~1/2 */
static inline uint32_t avalanche(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* Top 24 bits of a hash as a float in [-1, 1): exact in single precision */
static inline float lattice_value(uint32_t h)
{
    return (float)(int32_t)(avalanche(h) >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

static inline float noise_smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float noise_value_2d(float x, float z, uint32_t seed)
{
    float fx0 = floorf(x);
    float fz0 = floorf(z);
    int32_t ix = (int32_t)fx0;
    int32_t iz = (int32_t)fz0;

    uint32_t row0 = lattice_row(seed, ix);
    uint32_t row1 = lattice_row(seed, ix + 1);
    uint32_t w0 = (uint32_t)iz * WEYL_STEP;
    uint32_t w1 = w0 + WEYL_STEP;
    float v00 = lattice_value(row0 + w0);
    float v10 = lattice_value(row1 + w0);
    float v01 = lattice_value(row0 + w1);
    float v11 = lattice_value(row1 + w1);

    float sx = noise_smooth(x - fx0);
    float sz = noise_smooth(z - fz0);

    float nx0 = v00 + sx * (v10 - v00);
    float nx1 = v01 + sx * (v11 - v01);

    return nx0 + sz * (nx1 - nx0);
}

/* Octaves are separate noises, each with a hashed seed (seed + i * k would hand the
 * lattice hash related seeds). Their lattices are also shifted against each other by
 * steps of the R2 low-discrepancy sequence: with lacunarity 2 every octave would
 * otherwise put a lattice point, where value noise is flat, on each lattice point of the
 * first, and the sum would be flat on that grid. The shifts are constants rather than
 * seed bits so the shifted coordinates do not wait on the seed hash. */
#define OCTAVE_SHIFT_X 0.7548776662f /* 1 / plastic number */
#define OCTAVE_SHIFT_Z 0.5698402910f /* 1 / plastic number^2 */

static inline uint32_t octave_seed(uint32_t seed, int32_t octave)
{
    return avalanche(seed + (uint32_t)(octave + 1) * WEYL_STEP);
}

static inline float wrap_unit(float v)
{
    return v >= 1.0f ? v - 1.0f : v;
}

float noise_fbm_2d(float x, float z, uint32_t seed,
                    int32_t octaves, float lacunarity, float persistence)
{
    float value = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float shift_x = 0.0f;
    float shift_z = 0.0f;

    for (int32_t i = 0; i < octaves; i++)
    {
        value += noise_value_2d(x * freq + shift_x, z * freq + shift_z, octave_seed(seed, i)) * amp;
        amp *= persistence;
        freq *= lacunarity;
        shift_x = wrap_unit(shift_x + OCTAVE_SHIFT_X);
        shift_z = wrap_unit(shift_z + OCTAVE_SHIFT_Z);
    }

    return value;
}

float noise_ridged_2d(float x, float z, uint32_t seed,
                       int32_t octaves, float lacunarity, float persistence)
{
    float value = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float weight = 1.0f;
    float shift_x = 0.0f;
    float shift_z = 0.0f;

    for (int32_t i = 0; i < octaves; i++)
    {
        float n = noise_value_2d(x * freq + shift_x, z * freq + shift_z, octave_seed(seed, i));
        float signal = 1.0f - fabsf(n);
        signal *= signal;
        signal *= weight;
        weight = fminf(1.0f, fmaxf(0.0f, signal * 2.0f));

        value += signal * amp;
        amp *= persistence;
        freq *= lacunarity;
        shift_x = wrap_unit(shift_x + OCTAVE_SHIFT_X);
        shift_z = wrap_unit(shift_z + OCTAVE_SHIFT_Z);
    }

    return value;
}
