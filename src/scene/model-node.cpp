#include "lc1/scene/model-node.hpp"

namespace lc1::scene {

glm::mat4 NodeTransform::matrix() const
{
    auto finite = [](auto const &value) {
        for (glm::length_t i = 0; i < value.length(); ++i)
            if (!std::isfinite(value[i]))
                return false;
        return true;
    };
    float const length = glm::length(rotation);
    if (!finite(translation) || !finite(rotation) || !finite(scale) || !std::isfinite(length) ||
        length == 0.0F || scale.x == 0.0F || scale.y == 0.0F || scale.z == 0.0F)
        fail("model node transform must be finite and invertible");
    auto const result = glm::translate(glm::mat4{1.0F}, translation) *
                        glm::mat4_cast(rotation / length) * glm::scale(glm::mat4{1.0F}, scale);
    for (auto const column : {0, 1, 2, 3})
        if (!finite(result[column]))
            fail("model node transform matrix must be finite");
    return result;
}

} // namespace lc1::scene
