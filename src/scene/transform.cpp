#include "lc1/scene/transform.hpp"

namespace lc1::scene {

glm::mat4 Transform::model_matrix() const
{
    auto model = glm::translate(glm::mat4{1.0F}, position);
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3{1.0F, 0.0F, 0.0F});
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3{0.0F, 1.0F, 0.0F});
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3{0.0F, 0.0F, 1.0F});
    return glm::scale(model, scale);
}

} // namespace lc1::scene
