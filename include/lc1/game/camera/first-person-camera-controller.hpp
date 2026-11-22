#pragma once

#include <glm/glm.hpp>

#include <optional>

namespace lc1 {

struct CharacterPose;

namespace scene {
class FpsCamera;
} // namespace scene

class FirstPersonCameraController {
  public:
    void remember_angles(CharacterPose const &player);
    glm::vec2 remembered_angles() const;
    void update(scene::FpsCamera &camera, CharacterPose const &player,
                std::optional<glm::mat4> const &eye_transform) const;

  private:
    glm::vec2 remembered_angles_{};
};

} // namespace lc1
