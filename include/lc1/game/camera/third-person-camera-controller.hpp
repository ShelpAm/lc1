#pragma once

#include <glm/glm.hpp>

namespace lc1 {

namespace scene {
class FpsCamera;
} // namespace scene

class ThirdPersonCameraController {
  public:
    void set_angles(glm::vec2 angles);
    void orbit(glm::vec2 look_delta);
    glm::vec3 heading() const;
    void update(scene::FpsCamera &camera, glm::vec3 target) const;

  private:
    glm::vec2 angles_{};
};

} // namespace lc1
