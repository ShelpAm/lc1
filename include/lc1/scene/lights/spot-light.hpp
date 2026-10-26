#pragma once

#include "lc1/scene/lights/light-base.hpp"
#include "lc1/scene/lights/light.hpp"

#include <glm/glm.hpp>

namespace lc1::scene {

struct SpotLight {
    LightBase base;
    glm::vec3 position{0.0F};               // World space, in metres.
    glm::vec3 direction{0.0F, -1.0F, 0.0F}; // Unit direction in which light travels.
    // Unbounded inverse-square distance attenuation, weighted by the cone below.
    // Half-angles measured from direction, in radians:
    // 0 <= inner_angle <= outer_angle < pi/2.
    // Full angular strength inside inner_angle, fading to zero at outer_angle.
    // Equal angles specify a hard edge without a transition band.
    float inner_angle = glm::radians(20.0F);
    float outer_angle = glm::radians(30.0F);
};

Light to_light(SpotLight const &light);

} // namespace lc1::scene
