#include "lc1/scene/scene.hpp"

namespace lc1::scene {

Scene::Scene(FpsCamera const &camera, std::span<Light const> lights,
             std::span<DrawItem const> draws)
    : active_camera_(&camera), lights_(lights), draws_(draws)
{
}

FpsCamera const *Scene::active_camera() const
{
    return active_camera_;
}

std::span<Light const> Scene::lights() const
{
    return lights_;
}

std::span<DrawItem const> Scene::draws() const
{
    return draws_;
}

} // namespace lc1::scene
