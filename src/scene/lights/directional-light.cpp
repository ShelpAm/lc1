#include "lc1/scene/lights/directional-light.hpp"

namespace lc1::scene {

Light to_light(DirectionalLight const &light)
{
    return {.type = 0,
            .direction = light.direction,
            .color = light.base.color,
            .intensity = light.base.intensity};
}

} // namespace lc1::scene
