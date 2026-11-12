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

void Scene::set_diffuse_gi(std::optional<DiffuseGi> settings)
{
    if (settings) {
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(settings->center[axis]) ||
                !std::isfinite(settings->sky_radiance[axis]) || settings->sky_radiance[axis] < 0)
                fail("diffuse GI requires a finite center and finite nonnegative sky radiance");
    }
    diffuse_gi_ = settings;
}

std::optional<DiffuseGi> const &Scene::diffuse_gi() const
{
    return diffuse_gi_;
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
    if (!model.view_hidden_nodes.empty() &&
        model.view_hidden_nodes.size() != model.pose.model().nodes().size())
        fail("view visibility mask must match the model nodes");
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

void Scene::set_particles(std::vector<Particle> particles)
{
    for (auto const &particle : particles) {
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(particle.position[axis]) ||
                !std::isfinite(particle.radiance[axis]) || particle.radiance[axis] < 0)
                fail("particle position and radiance must be finite, radiance nonnegative");
        if (!std::isfinite(particle.size.x) || !std::isfinite(particle.size.y) ||
            particle.size.x <= 0 || particle.size.y <= 0 || !std::isfinite(particle.opacity) ||
            particle.opacity < 0 || particle.opacity > 1 || !std::isfinite(particle.rotation))
            fail("particle size must be positive, opacity in [0, 1], and rotation finite");
    }
    particles_ = std::move(particles);
}

std::span<Particle const> Scene::particles() const
{
    return particles_;
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
            auto const instances = model->pose.mesh_instances(model->world_transform);
            std::size_t draw_index = 0;
            for (std::size_t node = 0; node < model->pose.model().nodes().size(); ++node) {
                if (!model->pose.model().nodes()[node].mesh_index)
                    continue;
                auto const &mesh = instances[draw_index++];
                result.push_back({.mesh = &model->model->meshes()[mesh.mesh],
                                  .material = model->material,
                                  .model_matrix = mesh.model_matrix,
                                  .casts_shadow = model->casts_shadow,
                                  .depth_bias = model->depth_bias,
                                  .view_index_count =
                                      !model->view_visible || (model->use_view_indices &&
                                                               !model->view_hidden_nodes.empty() &&
                                                               model->view_hidden_nodes[node])
                                          ? std::optional<std::uint32_t>{0}
                                          : std::nullopt});
                for (auto const &skin : model->model->skins())
                    if (skin.node == node) {
                        result.back().skinned_vertices =
                            std::make_shared<std::vector<Vertex> const>(
                                model->pose.skin_vertices(skin));
                        result.back().skin_indices = skin.indices;
                        if (model->use_view_indices && !result.back().view_index_count)
                            result.back().view_index_count = skin.view_index_count;
                        break;
                    }
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
