#ifndef SHADOW_VOLUME_GLSL
#define SHADOW_VOLUME_GLSL

/*
 * shadow_volume.glsl - the terrain shadow volume (TerrainVolume): the opaque voxels only.
 * mip0 has a bit per voxel, 2x2x2 voxels per texel (bit (x & 1) + (y & 1) * 2 + (z & 1) * 4);
 * a texel of mip N (1..4) is nonzero when its block of 2^(N+1) voxels per axis holds an opaque
 * voxel, mip 4 being a chunk. The grid is whole chunks, so a voxel inside it is inside every mip.
 *
 * The including shader declares: uniform usampler3D shadow_volume_tex;
 */

/*
 * Level of the coarsest empty block around voxel p: a ray can jump the 2^(level + 1) voxels per
 * axis of block (p >> (level + 1)) << (level + 1). -1 when p's own 2x2x2 holds an opaque voxel;
 * occ0 gets that texel's bits. Every level is fetched at once: near the terrain, where most
 * levels are occupied, fetches that waited on each other were most of the cost of a step.
 */
int shadow_volume_empty_level(ivec3 p, out uint occ0) {
    uint occ4 = texelFetch(shadow_volume_tex, p >> 5, 4).r;
    uint occ3 = texelFetch(shadow_volume_tex, p >> 4, 3).r;
    uint occ2 = texelFetch(shadow_volume_tex, p >> 3, 2).r;
    uint occ1 = texelFetch(shadow_volume_tex, p >> 2, 1).r;
    occ0 = texelFetch(shadow_volume_tex, p >> 1, 0).r;

    int level = occ0 == 0u ? 0 : -1;
    level = occ1 == 0u ? 1 : level;
    level = occ2 == 0u ? 2 : level;
    level = occ3 == 0u ? 3 : level;
    return occ4 == 0u ? 4 : level;
}

/* Voxel p is opaque, given the mip0 texel holding it */
bool shadow_volume_opaque(uint occ0, ivec3 p) {
    uint bit = uint((p.x & 1) | ((p.y & 1) << 1) | ((p.z & 1) << 2));
    return (occ0 & (1u << bit)) != 0u;
}

#endif /* SHADOW_VOLUME_GLSL */
