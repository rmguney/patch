#include "game/world_scene_internal.h"
#include "game/terrain_gen.h"
#include "game/scatter_gen.h"
#include "game/shrub_gen.h"
#include "game/day_night.h"
#include "game/weather.h"
#include "engine/platform/thread.h"
#include "engine/core/profile.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/sim/detach.h"
#include "content/voxel_shapes.h"
#include "content/materials.h"
#include "content/scenes.h"
#include <stdlib.h>

/* Debris particles spawned per destroyed object/terrain region */
#define DEBRIS_MAX 64
/* Terrain destruction radius in voxels */
#define TERRAIN_DESTROY_RADIUS_VOXELS 2

static uint8_t pick_pastel_material(RngState *rng)
{
    return (uint8_t)(MAT_PASTEL_FIRST + rng_range_u32(rng, MAT_PASTEL_COUNT));
}

/* Adds a voxel object built from a shape. MAT_AIR keeps the shape's own materials,
 * any other material recolours every solid voxel. Returns the object index or -1. */
static int32_t spawn_shape(VoxelObjectWorld *world, const VoxelShape *shape, uint8_t material, Vec3 origin)
{
    if (!shape)
        return -1;

    if (material == MAT_AIR)
        return voxel_object_world_add_from_voxels(world, shape->voxels,
                                                  shape->size_x, shape->size_y, shape->size_z,
                                                  origin, world->voxel_size);

    int32_t total = voxel_shape_total_voxels(shape);
    uint8_t *voxels = (uint8_t *)malloc((size_t)total);
    if (!voxels)
        return -1;
    for (int32_t i = 0; i < total; i++)
        voxels[i] = shape->voxels[i] ? material : MAT_AIR;

    int32_t obj_idx = voxel_object_world_add_from_voxels(world, voxels,
                                                         shape->size_x, shape->size_y, shape->size_z,
                                                         origin, world->voxel_size);
    free(voxels);
    return obj_idx;
}

/* Places a shape on the terrain surface, offset from the world centre */
static void spawn_shape_on_terrain(WorldSceneData *data, Bounds3D bounds, int32_t shape_id,
                                   uint8_t material, float offset_x, float offset_z, uint32_t seed)
{
    float x = (bounds.min_x + bounds.max_x) * 0.5f + offset_x;
    float z = (bounds.min_z + bounds.max_z) * 0.5f + offset_z;
    float y = TERRAIN_BASE_HEIGHT + terrain_gen_height(x, z, data->params.terrain_amplitude,
                                                       data->params.terrain_frequency, seed);
    spawn_shape(data->objects, voxel_shape_get(shape_id), material, vec3_create(x, y, z));
}

/* A random registered shape in a random pastel colour */
static const VoxelShape *pick_random_shape(RngState *rng, uint8_t *out_material)
{
    const VoxelShape *shape = voxel_shape_get((int32_t)rng_range_u32(rng, (uint32_t)g_voxel_shape_count));
    *out_material = pick_pastel_material(rng);
    return shape;
}

/* Drops a random shape from the top of the world at a random spot.
 * Returns the object index, or -1 when the object world is full. */
static int32_t drop_random_shape(VoxelObjectWorld *world, Bounds3D bounds, RngState *rng)
{
    uint8_t material;
    const VoxelShape *shape = pick_random_shape(rng, &material);

    float x = bounds.min_x + 2.0f + rng_float(rng) * (bounds.max_x - bounds.min_x - 4.0f);
    float z = bounds.min_z + 2.0f + rng_float(rng) * (bounds.max_z - bounds.min_z - 4.0f);
    return spawn_shape(world, shape, material, vec3_create(x, bounds.max_y - 2.0f, z));
}

static void throw_random_shape(WorldSceneData *data, Scene *scene, Vec3 target)
{
    if (!data->objects || !data->physics)
        return;

    uint8_t material;
    const VoxelShape *shape = pick_random_shape(&scene->rng, &material);

    Vec3 spawn_pos = vec3_add(data->ray_origin, vec3_scale(data->ray_dir, 0.5f));
    int32_t obj_idx = spawn_shape(data->objects, shape, material, spawn_pos);
    if (obj_idx < 0)
        return;

    physics_world_sync_objects(data->physics);

    int32_t body_idx = physics_world_find_body_for_object(data->physics, obj_idx);
    if (body_idx >= 0)
    {
        Vec3 throw_dir = vec3_sub(target, spawn_pos);
        float dist = vec3_length(throw_dir);
        if (dist > 0.001f)
            throw_dir = vec3_scale(throw_dir, 1.0f / dist);
        else
            throw_dir = data->ray_dir;

        float throw_speed = 15.0f;
        Vec3 velocity = vec3_scale(throw_dir, throw_speed);
        physics_body_set_velocity(data->physics, body_idx, velocity);

        Vec3 spin = vec3_create(
            (rng_float(&scene->rng) - 0.5f) * 10.0f,
            (rng_float(&scene->rng) - 0.5f) * 10.0f,
            (rng_float(&scene->rng) - 0.5f) * 10.0f);
        physics_body_set_angular_velocity(data->physics, body_idx, spin);
    }

    data->stats.spawn_count++;
}

/* Bursts destroyed voxels outward from the impact point as particles, one RNG draw each.
 * The destroy calls record only the first DEBRIS_MAX of them. Returns the particle count. */
static int32_t spawn_debris(WorldSceneData *data, RngState *rng, Vec3 impact,
                            const Vec3 *positions, const uint8_t *materials, int32_t destroyed,
                            float voxel_size)
{
    if (!data->particles)
        return 0;

    int32_t count = destroyed < DEBRIS_MAX ? destroyed : DEBRIS_MAX;
    for (int32_t i = 0; i < count; i++)
    {
        Vec3 dir = vec3_sub(positions[i], impact);
        float dist = vec3_length(dir);
        if (dist > 0.001f)
            dir = vec3_scale(dir, 1.0f / dist);
        else
            dir = vec3_create(0.0f, 1.0f, 0.0f);

        float speed = 2.0f + rng_float(rng) * 2.0f;
        Vec3 velocity = vec3_scale(dir, speed);
        velocity.y += 1.0f;

        particle_system_add(data->particles, positions[i], velocity, material_get_color(materials[i]),
                            voxel_size * 0.4f);
    }
    return count;
}

/* Scene lighting for the current time of day and weather */
static void update_lighting(WorldSceneData *data, const SceneDescriptor *desc)
{
    data->lighting = desc->lighting;
    day_night_apply_lighting(&data->day_night, &data->lighting);
    weather_apply_lighting(&data->weather, &data->lighting);
}

/* Init stages. The weights are the stages' milliseconds measured on a 32-thread machine
 * generating with every thread; only their ratios matter, for the progress bar. */
static const LoadingStage s_init_stages[WORLD_SCENE_INIT_STAGES] = {
    {"Creating volume", 0.5f},
    {"Generating terrain", 75.0f},
    {"Filling water", 50.0f},
    {"Planting trees", 18.0f},
    {"Growing shrubs", 1.0f},
    {"Scattering flora", 17.0f},
    {"Counting voxels", 0.5f},
    {"Placing objects", 0.5f},
    {"Starting physics", 13.0f},
};

/* The loading screen gets a frame this often while a stage runs */
#define LOADING_FRAME_INTERVAL_MS 16

/* A positive whole number from the environment variable name, or 0 when it is unset or
 * holds anything else */
static int32_t env_positive_int(const char *name)
{
    const char *text = getenv(name);
    if (!text)
        return 0;
    char *end = NULL;
    long long value = strtoll(text, &end, 10);
    return end != text && *end == '\0' && value > 0 && value <= INT32_MAX ? (int32_t)value : 0;
}

/* World generation threads: PATCH_GEN_THREADS when set (tests and measurements force a
 * count; more threads than the hardware runs would only take turns on it, so the count
 * stops there). Otherwise every hardware thread, but one is left to the thread that keeps
 * presenting loading screen frames meanwhile and the driver work its frames need;
 * generating on that one too measured no faster on 32 threads */
static int32_t generation_thread_count(bool presenting)
{
    int32_t hardware = platform_thread_hardware_count();
    int32_t forced = env_positive_int("PATCH_GEN_THREADS");
    if (forced > 0)
        return forced < hardware ? forced : hardware;
    return presenting && hardware > 1 ? hardware - 1 : hardware;
}

/* On the loader thread while the app shows the loading screen: the stages touch only the
 * scene, which nothing else uses until the load is done */
void world_scene_run_init_stage(void *ctx, int32_t stage)
{
    Scene *scene = (Scene *)ctx;
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    const WorldSceneParams *p = &data->params;
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);

    /* Without the volume (out of memory) there is no terrain to generate */
    if (stage >= 1 && stage <= 6 && !data->terrain)
        return;

    switch (stage)
    {
    case 0:
    {
        Vec3 origin = vec3_create(scene->bounds.min_x, scene->bounds.min_y, scene->bounds.min_z);
        data->terrain = volume_create_dims(desc->chunks_x, desc->chunks_y, desc->chunks_z,
                                           origin, data->voxel_size);
        break;
    }
    case 1:
        /* Out of memory leaves the columns empty (nx = nz = 0): the generation stages
         * after it find no columns and write nothing */
        if (terrain_columns_build(&data->columns, data->terrain, p->terrain_amplitude,
                                  p->terrain_frequency, desc->rng_seed, data->jobs))
            terrain_gen_heightmap(data->terrain, &data->columns, data->jobs);
        terrain_gen_pillars(data->terrain, p->num_pillars, p->terrain_amplitude, p->terrain_frequency,
                            desc->rng_seed);
        break;
    case 2:
        terrain_gen_water(data->terrain, &data->columns, data->jobs);
        break;
    case 3:
        terrain_gen_trees(data->terrain, &data->columns, p->tree_density, data->jobs);
        break;
    case 4:
        shrub_gen_place(data->terrain, &data->columns);
        break;
    case 5:
        scatter_gen_apply(data->terrain, &data->columns, data->jobs);
        terrain_columns_release(&data->columns);
        break;
    case 6:
        /* Every generation write kept its chunk's occupancy exact: only the volume's
         * counters are behind */
        volume_recount_solid(data->terrain);
        data->detach_ready = connectivity_work_init(&data->detach_work, data->terrain);
        break;
    case 7:
    {
        data->objects = voxel_object_world_create(scene->bounds, data->voxel_size);
        spawn_shape_on_terrain(data, scene->bounds, SHAPE_GARY, MAT_AIR, 3.0f, 3.0f, desc->rng_seed);
        spawn_shape_on_terrain(data, scene->bounds, SHAPE_CUBE, MAT_METAL, 6.0f, 3.0f, desc->rng_seed);

        /* PATCH_STRESS_OBJECTS (render perf tests) drops that many random shapes in;
         * spawning stops early once the object world is full */
        const int32_t spawn_target = env_positive_int("PATCH_STRESS_OBJECTS");
        data->stats.spawn_count = 0;
        while (data->stats.spawn_count < spawn_target &&
               drop_random_shape(data->objects, scene->bounds, &scene->rng) >= 0)
            data->stats.spawn_count++;
        break;
    }
    case 8:
        data->particles = particle_system_create(scene->bounds);
        data->env_particles = env_particles_create(desc->rng_seed);
        if (data->env_particles && data->terrain)
            env_particles_register_emitters(data->env_particles, data->terrain, data->voxel_size, data->jobs);
        data->physics = physics_world_create(data->objects, data->terrain);
        data->day_night = day_night_create(300.0f);
        data->weather = weather_create(desc->rng_seed);
        update_lighting(data, desc);
        break;
    default:
        break;
    }
}

/* The generation threads are only needed while loading */
static void world_scene_end_load(WorldSceneData *data, LoadingState *state)
{
    job_system_destroy(data->jobs);
    data->jobs = NULL;
    data->loaded = true;
    loading_state_complete(state);
}

/* Runs every init stage on the calling thread */
static void world_scene_load_here(Scene *scene, LoadingState *state)
{
    for (int32_t stage = 0; stage < WORLD_SCENE_INIT_STAGES; stage++)
    {
        loading_state_advance(state, s_init_stages[stage].name);
        loading_log_stage(stage, WORLD_SCENE_INIT_STAGES, s_init_stages[stage].name);
        world_scene_run_init_stage(scene, stage);
    }
    world_scene_end_load((WorldSceneData *)scene->user_data, state);
}

bool world_scene_init_step(Scene *scene, LoadingState *state)
{
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    if (data->loaded)
        return true;

    if (!data->loader)
    {
        data->jobs = job_system_create(generation_thread_count(true));
        data->loader = loading_start(s_init_stages, WORLD_SCENE_INIT_STAGES, world_scene_run_init_stage, scene);
        if (!data->loader)
        {
            /* No loader thread: load here, the screen standing still meanwhile */
            world_scene_load_here(scene, state);
            return true;
        }
    }

    if (!loading_poll(data->loader, state, LOADING_FRAME_INTERVAL_MS))
        return false;
    loading_finish(data->loader);
    data->loader = NULL;
    world_scene_end_load(data, state);
    return true;
}

/* Loads on the calling thread (scene_init: tests and tools), still generating with every
 * hardware thread */
static void world_scene_init(Scene *scene)
{
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    LoadingState ls;
    loading_state_init(&ls, WORLD_SCENE_INIT_STAGES);
    data->jobs = job_system_create(generation_thread_count(false));
    world_scene_load_here(scene, &ls);
}

static void world_scene_destroy_impl(Scene *scene)
{
    WorldSceneData *data = (WorldSceneData *)scene->user_data;

    /* Abandoned while loading: the stage in progress finishes first */
    loading_finish(data->loader);
    job_system_destroy(data->jobs);

    if (data->physics)
        physics_world_destroy(data->physics);
    if (data->env_particles)
        env_particles_destroy(data->env_particles);
    if (data->particles)
        particle_system_destroy(data->particles);
    if (data->objects)
        voxel_object_world_destroy(data->objects);

    connectivity_work_destroy(&data->detach_work);
    terrain_columns_release(&data->columns);

    if (data->terrain)
        volume_destroy(data->terrain);

    free(data);
    free(scene);
}

static void dig_at_ray(WorldSceneData *data, Scene *scene);
static void process_pending_connectivity(WorldSceneData *data, Scene *scene);

static void world_scene_tick(Scene *scene)
{
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    const float dt = SIM_TIMESTEP;

    PROFILE_BEGIN(PROFILE_SIM_TICK);

    data->stats.tick_count++;

    /* Drawing interpolates the objects from here to where this tick leaves them */
    voxel_object_world_begin_tick(data->objects);

    /* Left button: carving while it is held (and for a press that ended between ticks),
     * once it is up the launch of whatever the carving cut loose */
    if (data->left_held || data->left_pressed)
        dig_at_ray(data, scene);
    else if (data->pending_connectivity && data->detach_ready && data->terrain && data->objects)
        process_pending_connectivity(data, scene);
    data->left_pressed = false;

    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    day_night_update(&data->day_night, dt);
    weather_update(&data->weather, dt);
    update_lighting(data, desc);

    weather_accumulate_snow(&data->weather, data->terrain, dt);

    PROFILE_BEGIN(PROFILE_SIM_PARTICLES);
    particle_system_update(data->particles, dt, data->terrain, data->objects);
    if (data->env_particles)
    {
        /* Rain and snow fall around the viewer: the camera the pick ray starts from */
        weather_apply_env_particles(&data->weather, data->env_particles, data->ray_origin, dt);
        double sim_time = (double)data->stats.tick_count * (double)dt;
        env_particles_update(data->env_particles, data->terrain, day_night_daylight(&data->day_night), dt, sim_time);
    }
    PROFILE_END(PROFILE_SIM_PARTICLES);

    /* Process deferred voxel object work (budgeted per-frame) */
    if (data->objects)
    {
        voxel_object_world_process_splits(data->objects);
        voxel_object_world_process_recalcs(data->objects);
        voxel_object_world_tick_render_delays(data->objects);
    }

    if (data->physics)
    {
        physics_world_sync_objects(data->physics);
        physics_world_step(data->physics, dt);
    }

    /* Object BVH (picking raycasts, particle point tests), after physics moved things */
    if (data->objects)
        voxel_object_world_update_bvh(data->objects);

    PROFILE_END(PROFILE_SIM_TICK);
}

WorldSceneDestruction world_scene_destroy_sphere(Scene *scene, Vec3 center, int32_t radius_voxels)
{
    WorldSceneDestruction result = {0};
    if (!scene || !scene->user_data)
        return result;
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    if (!data->terrain)
        return result;

    const float vs = data->terrain->voxel_size;
    Vec3 destroyed_positions[DEBRIS_MAX];
    uint8_t destroyed_materials[DEBRIS_MAX];
    result.voxels_removed = detach_terrain_at_point(data->terrain, center, radius_voxels,
                                                    destroyed_positions, destroyed_materials, DEBRIS_MAX);

    /* Bodies resting on the carved voxels wake through the volume's removal log in the next
     * physics step */
    result.debris_spawned = spawn_debris(data, &scene->rng, center, destroyed_positions, destroyed_materials,
                                         result.voxels_removed, vs);

    /* Deferred: a held button keeps carving, and one analysis after it covers every cut */
    data->pending_connectivity = true;
    data->last_destroy_point = center;
    return result;
}

/* Turns terrain cut loose by earlier destruction into falling voxel objects. Runs at most
 * once per tick. */
static void process_pending_connectivity(WorldSceneData *data, Scene *scene)
{
    DetachConfig cfg = detach_config_default();
    DetachResult detach_result;
    detach_terrain_process(data->terrain, data->objects, &cfg, &data->detach_work, &detach_result,
                           data->particles, &scene->rng);

    /* Apply outward velocity to newly spawned islands */
    if (detach_result.bodies_spawned > 0 && data->physics)
    {
        physics_world_sync_objects(data->physics);

        int32_t count = detach_result.bodies_spawned;
        if (count > DETACH_MAX_SPAWNED)
            count = DETACH_MAX_SPAWNED;

        for (int32_t i = 0; i < count; i++)
        {
            int32_t obj_idx = detach_result.spawned_indices[i];
            VoxelObject *obj = &data->objects->objects[obj_idx];
            if (!obj->active)
                continue;

            int32_t body_idx = physics_world_find_body_for_object(data->physics, obj_idx);
            if (body_idx < 0)
                continue;

            Vec3 dir = vec3_sub(obj->position, data->last_destroy_point);
            float dist = vec3_length(dir);
            if (dist > 0.001f)
                dir = vec3_scale(dir, 1.0f / dist);
            else
                dir = vec3_create(0.0f, 1.0f, 0.0f);

            float speed = 3.0f;
            Vec3 velocity = vec3_scale(dir, speed);
            velocity.y += 1.5f;
            physics_body_set_velocity(data->physics, body_idx, velocity);

            RigidBody *body = physics_world_get_body(data->physics, body_idx);
            if (body)
            {
                body->flags &= ~PHYS_FLAG_GROUNDED;
                body->ground_frames = 0;
            }
        }
    }

    /* Floating islands left over (result slots full) are handled next tick */
    data->pending_connectivity = detach_result.analysis_requeued;
}

/* One carve where the pick ray meets an object or the terrain, whichever is nearer */
static void dig_at_ray(WorldSceneData *data, Scene *scene)
{
    VoxelObjectHit obj_hit = {0};
    float obj_dist = 1e30f;

    bool terrain_hit = false;
    float terrain_dist = 1e30f;
    Vec3 terrain_hit_pos = vec3_zero();
    Vec3 terrain_hit_normal = vec3_zero();
    uint8_t terrain_mat = 0;

    if (data->objects)
    {
        obj_hit = voxel_object_world_raycast(data->objects, data->ray_origin, data->ray_dir);
        if (obj_hit.hit)
            obj_dist = vec3_length(vec3_sub(obj_hit.impact_point, data->ray_origin));
    }

    if (data->terrain)
    {
        terrain_dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir,
                                      100.0f, &terrain_hit_pos, &terrain_hit_normal, &terrain_mat,
                                      MAT_FLAG_LIQUID);
        terrain_hit = (terrain_dist >= 0.0f && terrain_mat != 0);
    }

    if (obj_hit.hit && (!terrain_hit || obj_dist <= terrain_dist))
    {
        Vec3 destroyed_positions[DEBRIS_MAX];
        uint8_t destroyed_materials[DEBRIS_MAX];

        float voxel_size = data->objects->voxel_size;
        int32_t destroyed = detach_object_at_point(
            data->objects, obj_hit.object_index, obj_hit.impact_point, voxel_size * 1.5f,
            destroyed_positions, destroyed_materials, DEBRIS_MAX);
        spawn_debris(data, &scene->rng, obj_hit.impact_point, destroyed_positions, destroyed_materials,
                     destroyed, voxel_size);
    }
    else if (terrain_hit)
    {
        /* The hit point lies on the hit voxel's face, where rounding can drop that voxel from
         * the carve lattice and a held button stalls on the face: centre the carve inside it */
        Vec3 center = vec3_sub(terrain_hit_pos, vec3_scale(terrain_hit_normal, data->terrain->voxel_size * 0.5f));
        world_scene_destroy_sphere(scene, center, TERRAIN_DESTROY_RADIUS_VOXELS);
    }
}

static void world_scene_handle_input(Scene *scene, float mouse_x, float mouse_y, bool left_down, bool right_down)
{
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    (void)mouse_x;
    (void)mouse_y;

    if (right_down && !data->right_was_down)
    {
        Vec3 target = vec3_add(data->ray_origin, vec3_scale(data->ray_dir, 20.0f));

        if (data->terrain)
        {
            Vec3 terrain_hit_pos = vec3_zero();
            Vec3 terrain_hit_normal = vec3_zero();
            uint8_t terrain_mat = 0;
            float terrain_dist = volume_raycast(data->terrain, data->ray_origin, data->ray_dir,
                                               100.0f, &terrain_hit_pos, &terrain_hit_normal, &terrain_mat,
                                               MAT_FLAG_LIQUID);
            if (terrain_dist >= 0.0f && terrain_mat != 0)
                target = terrain_hit_pos;
        }

        throw_random_shape(data, scene, target);
    }
    data->right_was_down = right_down;

    /* The left button acts in the ticks, at the tick rate whatever the frame rate */
    data->left_held = left_down;
    data->left_pressed |= left_down;
}

static const char *world_scene_get_name(Scene *scene)
{
    (void)scene;
    return "World";
}

static const SceneVTable world_scene_vtable = {
    .init = world_scene_init,
    .destroy = world_scene_destroy_impl,
    .tick = world_scene_tick,
    .handle_input = world_scene_handle_input,
    .get_name = world_scene_get_name};

WorldSceneParams world_scene_default_params(void)
{
    WorldSceneParams p;
    p.num_pillars = 40;
    p.terrain_amplitude = 8.0f;
    p.terrain_frequency = 0.06f;
    p.tree_density = 0.6f;
    return p;
}

Scene *world_scene_create(Bounds3D bounds, float voxel_size, const WorldSceneParams *params)
{
    Scene *scene = (Scene *)calloc(1, sizeof(Scene));
    if (!scene)
        return NULL;

    WorldSceneData *data = (WorldSceneData *)calloc(1, sizeof(WorldSceneData));
    if (!data)
    {
        free(scene);
        return NULL;
    }

    data->params = params ? *params : world_scene_default_params();
    data->voxel_size = voxel_size;
    data->ray_dir = vec3_create(0.0f, 0.0f, -1.0f);

    scene->vtable = &world_scene_vtable;
    scene->bounds = bounds;
    scene->user_data = data;
    rng_seed(&scene->rng, 12345);

    return scene;
}

void world_scene_set_ray(Scene *scene, Vec3 origin, Vec3 dir)
{
    if (!scene || !scene->user_data)
        return;
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    data->ray_origin = origin;
    data->ray_dir = dir;
}

VoxelVolume *world_scene_get_terrain(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((WorldSceneData *)scene->user_data)->terrain;
}

VoxelObjectWorld *world_scene_get_objects(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((WorldSceneData *)scene->user_data)->objects;
}

ParticleSystem *world_scene_get_particles(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((WorldSceneData *)scene->user_data)->particles;
}

EnvParticleSystem *world_scene_get_env_particles(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return ((WorldSceneData *)scene->user_data)->env_particles;
}

const SceneLighting *world_scene_get_lighting(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return &((WorldSceneData *)scene->user_data)->lighting;
}

const WorldSceneStats *world_scene_get_stats(Scene *scene)
{
    if (!scene || !scene->user_data)
        return NULL;
    return &((WorldSceneData *)scene->user_data)->stats;
}

void world_scene_set_time_of_day(Scene *scene, float time)
{
    if (!scene || !scene->user_data)
        return;
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    data->day_night.time_of_day = time;
}

void world_scene_set_weather(Scene *scene, int weather_state)
{
    if (!scene || !scene->user_data)
        return;
    if (weather_state < 0 || weather_state >= WEATHER_COUNT)
        return;
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    weather_set_state(&data->weather, (WeatherState)weather_state);
}
