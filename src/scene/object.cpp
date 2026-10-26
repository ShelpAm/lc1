#include "lc1/scene/object.hpp"

namespace lc1::scene {

DrawItem Object::draw_item() const
{
    return {.mesh = mesh,
            .material = material,
            .model_matrix = transform.model_matrix(),
            .casts_shadow = casts_shadow,
            .depth_bias = false};
}

} // namespace lc1::scene
