#pragma once

#include "lc1/scene/lights/light-base.hpp"
#include "lc1/scene/lights/light.hpp"

#include <glm/glm.hpp>

namespace lc1::scene {

struct PointLight {
    LightBase base;
    glm::vec3 position{0.0F}; // World space, in metres.
    // Unbounded inverse-square distance attenuation.
};

Light to_light(PointLight const &light);

} // namespace lc1::scene
