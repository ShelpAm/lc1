#pragma once

#include "lc1/scene/lights/light-base.hpp"
#include "lc1/scene/lights/light.hpp"

#include <glm/glm.hpp>

namespace lc1::scene {

// Scene data only; independent of GPU buffer layouts and Vulkan resources.
struct DirectionalLight {
    LightBase base;
    // Unit world-space direction in which light travels (Y is up).
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    // Linear RGB tint, nonnegative; white preserves the intensity's photometric value.
};

Light to_light(DirectionalLight const &light);

} // namespace lc1::scene
