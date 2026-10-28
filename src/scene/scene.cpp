#include "lc1/scene/scene.hpp"
#include "lc1/error.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

#include <cmath>

namespace lc1::scene {
namespace {
template <typename Collection, typename T> void remove_owned(Collection &entries, T const *entry)
{
    auto const found = entries.find(entry);
    if (found == entries.end())
        fail("object does not belong to this scene");
    entries.erase(found);
}
} // namespace

ModelDefinition const &SceneModel::definition(GpuModel const *model)
{
    if (!model)
        fail("scene model requires a GPU model");
    return model->definition();
}

glm::mat4 SceneModel::node_world_matrix(std::size_t node) const
{
    return pose.node_world_matrix(node, world_transform);
}

SceneLight *Scene::add_light(Light light)
{
    auto owned = std::make_unique<SceneLight>(SceneLight{light});
    auto *pointer = owned.get();
    lights_.emplace(pointer, std::move(owned));
    return pointer;
}

void Scene::remove_light(SceneLight const *light)
{
    remove_owned(lights_, light);
}

SceneModel *Scene::add_model(SceneModel model)
{
    if (!model.material)
        fail("scene model requires a material");
    if (!model.model || &model.pose.model() != &model.model->definition())
        fail("scene model pose does not match its GPU model");
    auto owned = std::make_unique<SceneModel>(std::move(model));
    auto *pointer = owned.get();
    models_.emplace(pointer, std::move(owned));
    return pointer;
}

void Scene::remove_model(SceneModel const *model)
{
    remove_owned(models_, model);
}

void Scene::update_animations(float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F)
        fail("scene animation delta must be finite and nonnegative");
    for (auto const &[pointer, model] : models_)
        model->pose.update_animation(delta_seconds);
}

std::vector<Light> Scene::lights() const
{
    std::vector<Light> result;
    result.reserve(lights_.size());
    for (auto const &[pointer, light] : lights_)
        if (light->enabled)
            result.push_back(light->parameters);
    return result;
}

std::vector<DrawItem> Scene::draws() const
{
    std::vector<DrawItem> result;
    result.reserve(draw_count());
    for (auto const &[pointer, model] : models_)
        if (model->visible) {
            if (!model->material || !model->model ||
                &model->pose.model() != &model->model->definition())
                fail("scene model has invalid resource bindings");
            for (auto const &mesh : model->pose.mesh_instances(model->world_transform)) {
                result.push_back({.mesh = &model->model->meshes()[mesh.mesh],
                                  .material = model->material,
                                  .model_matrix = mesh.model_matrix,
                                  .casts_shadow = model->casts_shadow,
                                  .depth_bias = model->depth_bias});
            }
        }
    return result;
}

std::size_t Scene::draw_count() const
{
    std::size_t count = 0;
    for (auto const &[pointer, model] : models_)
        count += model->pose.model().draw_count();
    return count;
}

} // namespace lc1::scene
