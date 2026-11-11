#pragma once

#include "lc1/scene/camera.hpp"
#include "lc1/scene/lights/light.hpp"
#include "lc1/scene/scene.hpp"
#include "lc1/vk/render/draw-item.hpp"

#include <vector>

namespace lc1 {

// Immutable CPU inputs shared by the callbacks registered for one frame.
// DrawItem's mesh and material pointers still borrow GPU resource owners.
struct FrameScene {
    scene::FpsCamera camera;
    std::vector<scene::Light> lights;
    std::vector<DrawItem> draws;
    std::optional<scene::DiffuseGi> diffuse_gi;
};

} // namespace lc1
