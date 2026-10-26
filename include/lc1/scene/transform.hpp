#pragma once

#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace lc1::scene {

struct Transform {
    glm::vec3 position{0.0F};
    glm::vec3 rotation{0.0F}; // Degrees, counter-clockwise around each axis.
    glm::vec3 scale{1.0F};

    glm::mat4 model_matrix() const;
};

} // namespace lc1::scene
