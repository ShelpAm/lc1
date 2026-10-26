#include "lc1/scene/lights/sphere-light.hpp"

namespace lc1::scene {

Light to_light(SphereLight const &light)
{
    return {.position = light.position,
            .type = 3,
            .radius = light.radius,
            .color = light.base.color,
            .intensity = light.base.intensity,
            .shadow_sample_count = light.shadow_sample_count};
}

} // namespace lc1::scene
