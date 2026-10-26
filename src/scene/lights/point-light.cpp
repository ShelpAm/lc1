#include "lc1/scene/lights/point-light.hpp"

namespace lc1::scene {

Light to_light(PointLight const &light)
{
    return {.position = light.position,
            .type = 1,
            .color = light.base.color,
            .intensity = light.base.intensity};
}

} // namespace lc1::scene
