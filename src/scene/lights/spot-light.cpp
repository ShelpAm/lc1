#include "lc1/scene/lights/spot-light.hpp"

namespace lc1::scene {

Light to_light(SpotLight const &light)
{
    return {.position = light.position,
            .type = 2,
            .direction = light.direction,
            .color = light.base.color,
            .intensity = light.base.intensity,
            .spot_angles = {light.inner_angle, light.outer_angle}};
}

} // namespace lc1::scene
