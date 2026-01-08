#include "melee_renderer.h"
#include "../engine/renderer.h"
#include "../game/player.h"
#include "../game/enemy.h"
#include "../game/humanoid.h"

namespace patch {

static void draw_player(const Player* player, Renderer& renderer, float floor_y) {
    Vec3 base = player->position;
    base.y = floor_y;

    Vec3 player_color = vec3_create(0.20f, 0.60f, 0.85f);

    if (player->is_dead) {
        player_color = vec3_create(0.3f, 0.4f, 0.5f);
    }

    HumanoidPose pose = player_get_pose(player);

    renderer.draw_humanoid_voxels(base, &player->model, &pose, player_color);
}

static void draw_enemy(const Enemy* enemy, Renderer& renderer, float floor_y) {
    if (!enemy->active) return;

    Vec3 base = enemy->position;
    base.y = floor_y;

    Vec3 enemy_color = vec3_create(0.85f, 0.45f, 0.45f);

    HumanoidPose pose = enemy_get_pose(enemy);

    if (enemy->state != ENEMY_STATE_DYING && enemy->state != ENEMY_STATE_DEAD) {
        renderer.draw_humanoid_voxels(base, &enemy->model, &pose, enemy_color);
    } else {
        enemy_color = vec3_create(0.5f, 0.25f, 0.25f);
        renderer.draw_humanoid_ragdoll(&enemy->model, enemy_color);
    }
}

void melee_render(Scene* scene, Renderer& renderer) {
    if (!scene || !scene->user_data) return;

    MeleeData* data = static_cast<MeleeData*>(scene->user_data);

    float floor_y = scene->bounds.min_y;

    const float chunk_size = 11.0f;
    const int tile_chunks = 13;
    float tile_size = chunk_size * (float)tile_chunks;

    int32_t player_cx = (int32_t)floorf(data->player.position.x / chunk_size);
    int32_t player_cz = (int32_t)floorf(data->player.position.z / chunk_size);
    float cx = ((float)player_cx + 0.5f) * chunk_size;
    float cz = ((float)player_cz + 0.5f) * chunk_size;

    Vec3 floor_color = vec3_create(0.68f, 0.85f, 0.92f);
    renderer.draw_box(vec3_create(cx, floor_y - 0.15f, cz),
                      vec3_create(tile_size, 0.3f, tile_size),
                      floor_color, 1.0f);

    for (int32_t i = 0; i < data->vobj_world->object_count; i++) {
        VoxelObject* obj = &data->vobj_world->objects[i];
        if (obj->active) {
            renderer.draw_voxel_object(obj);
        }
    }

    for (int32_t i = 0; i < data->enemy_count; i++) {
        draw_enemy(&data->enemies[i], renderer, floor_y);
    }

    draw_player(&data->player, renderer, floor_y);

    renderer.draw_particles(data->particles);

    if (data->player.is_dead) {
        renderer.draw_bricked_text(data->survival_time, data->destroyed_cubes);
    }
}

}
