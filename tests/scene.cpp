#include "lc1/scene/scene.hpp"
#include "lc1/error.hpp"
#include "lc1/vk/render/material.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename F> void rejects(F &&action)
{
    try {
        action();
    }
    catch (lc1::Error const &) {
        return;
    }
    throw std::runtime_error("expected scene validation failure");
}

void scene_lifetime_and_snapshots()
{
    lc1::scene::Scene scene;
    auto *sun = scene.add_light({.intensity = 3.0F});
    auto const lights = scene.lights();
    sun->parameters.intensity = 8.0F;
    check(lights[0].intensity == 3.0F, "snapshots survive light updates");
    sun->enabled = false;
    check(scene.lights().empty(), "disabled lights are not submitted");
    sun->enabled = true;
    std::vector<lc1::scene::SceneLight *> additions;
    for (int i = 0; i < 256; ++i)
        additions.push_back(scene.add_light({.intensity = static_cast<float>(i)}));
    check(sun->parameters.intensity == 8.0F, "rehashing preserves light pointers");
    for (auto *light : additions)
        scene.remove_light(light);
    check(sun->parameters.intensity == 8.0F && scene.lights().size() == 1,
          "removing other lights preserves borrowed pointers");
    lc1::scene::SceneLight foreign{};
    rejects([&] { scene.remove_light(&foreign); });
    rejects([&] { scene.remove_light(nullptr); });
    scene.remove_light(sun);
    check(scene.lights().empty(), "removal releases the scene light");
    check(lights[0].intensity == 3.0F, "CPU snapshots survive removal");
}

void model_ownership_animation_and_mounts()
{
    using namespace lc1::scene;
    ModelDefinition const definition{
        0,
        {{.name = "Root"}, {.name = "Eyes", .parent = 0, .transform = {.translation = {0, 3, 0}}}},
        {{.name = "Move",
          .channels = {
              {.times = {0, 2}, .values = std::vector<glm::vec3>{{0, 0, 0}, {4, 0, 0}}}}}}};
    GpuModel const model{std::vector<lc1::GpuMesh>{}, definition};
    lc1::MaterialInfo const material{};
    Scene scene;
    auto *player = scene.add_model({.model = &model, .material = &material});
    auto *npc = scene.add_model({.model = &model, .material = &material});
    player->world_transform[3].x = 10;
    player->pose.play_animation(0, false);
    scene.update_animations(1);
    auto const mount = player->node_world_matrix(1);
    check(mount[3].x == 12 && mount[3].y == 3,
          "scene mount combines submitted world transform and animated hierarchy");
    check(npc->node_world_matrix(1)[3].x == 0,
          "shared definition preserves independent model poses");
    player->visible = false;
    scene.update_animations(1);
    check(player->node_world_matrix(1)[3].x == 14,
          "hidden models still advance animations and expose mounts");
    check(mount[3].x == 12, "mount query returns a value snapshot");
    check(scene.draws().empty() && scene.draw_count() == 0,
          "mesh-less mounts do not consume draw capacity");
    std::vector<SceneModel *> additions;
    for (int i = 0; i < 256; ++i)
        additions.push_back(scene.add_model({.model = &model, .material = &material}));
    check(player->node_world_matrix(1)[3].x == 14, "rehashing preserves model pointers");
    for (auto *model : additions)
        scene.remove_model(model);
    check(npc->node_world_matrix(1)[3].y == 3, "removing other models preserves pointers");
    rejects([&] { (void)player->node_world_matrix(2); });
    rejects([&] { scene.update_animations(-1); });
    rejects([&] { scene.add_model({.model = &model, .material = nullptr}); });
    ModelDefinition const missing_mesh{1, {{.name = "Mesh", .mesh = 0}}};
    rejects([&] { GpuModel invalid{std::vector<lc1::GpuMesh>{}, missing_mesh}; });
    rejects([&] { scene.add_model({.model = nullptr, .material = &material}); });
    GpuModel const other_model{std::vector<lc1::GpuMesh>{}, definition};
    rejects([&] {
        scene.add_model({.model = &model,
                         .pose = ModelInstance{other_model.definition()},
                         .material = &material});
    });
    npc->model = &other_model;
    rejects([&] { (void)scene.draws(); });
    npc->model = &model;
    Scene other;
    auto *foreign = other.add_model({.model = &model, .material = &material});
    rejects([&] { scene.remove_model(foreign); });
    rejects([&] { scene.remove_model(nullptr); });
    scene.remove_model(player);
    check(npc->node_world_matrix(1)[3].y == 3, "removing one model preserves other instances");
}
} // namespace

int main()
{
    try {
        scene_lifetime_and_snapshots();
        model_ownership_animation_and_mounts();
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
