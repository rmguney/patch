#ifndef HDDA_TYPES_GLSL
#define HDDA_TYPES_GLSL

struct HitInfo {
    bool hit;
    vec3 pos;           /* World space hit position */
    vec3 normal;        /* World space surface normal */
    float t;            /* Ray parameter at hit */
    vec3 color;         /* Resolved material color */
    float emissive;     /* Emissive intensity */
    float roughness;    /* Surface roughness */
    float metallic;     /* Metallic factor */
    float water_depth;  /* World-unit depth through water (0 = no water) */
};

struct DDAState {
    ivec3 map_pos;      /* Current voxel position */
    ivec3 step_dir;     /* Step direction per axis (-1 or +1) */
    vec3 side_dist;     /* Distance to next voxel boundary per axis */
    vec3 delta_dist;    /* Distance between boundaries per axis */
    bvec3 last_mask;    /* Which axis was stepped last (for normal) */
};

const int CHUNK_SIZE = 32;          /* Voxels per chunk dimension */
const int REGION_SIZE = 8;          /* Voxels per region dimension (8x8x8) */
const int CHUNK_UINT_COUNT = 8192;  /* uint32s per chunk for voxel data (32768 voxels / 4) */

#endif /* HDDA_TYPES_GLSL */
