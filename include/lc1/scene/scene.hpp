#pragma once

#include "lc1/scene/camera.hpp"
#include "lc1/scene/lights/light.hpp"
#include "lc1/vk/render/draw-item.hpp"

#include <span>

namespace lc1::scene {

// Frame-local scene input. Camera, lights and draws are borrowed until Renderer snapshots
// them in record/add_passes. Referenced GPU resources must survive GPU execution.
class Scene {
  public:
    Scene() = default;

    Scene(FpsCamera const &camera, std::span<Light const> lights, std::span<DrawItem const> draws);

    FpsCamera const *active_camera() const;

    std::span<Light const> lights() const;

    std::span<DrawItem const> draws() const;

  private:
    FpsCamera const *active_camera_ = nullptr;
    std::span<Light const> lights_;
    std::span<DrawItem const> draws_;
};

} // namespace lc1::scene
