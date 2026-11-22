#include "lc1/game/camera/third-person-camera-controller.hpp"

#include "lc1/scene/camera.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {

void ThirdPersonCameraController::set_angles(glm::vec2 angles)
{
    angles_ = angles;
}

void ThirdPersonCameraController::orbit(glm::vec2 look_delta)
{
    angles_.x -= look_delta.x * 0.05F;
    angles_.y = std::clamp(angles_.y - look_delta.y * 0.05F, -60.0F, 60.0F);
}

glm::vec3 ThirdPersonCameraController::heading() const
{
    auto const yaw = glm::radians(angles_.x);
    return {-std::sin(yaw), 0.0F, -std::cos(yaw)};
}

void ThirdPersonCameraController::update(scene::FpsCamera &camera, glm::vec3 target) const
{
    auto const direction = heading();
    camera.set_position(target - direction * 4.0F + glm::vec3{0, 2.0F, 0});
    camera.look_at(target + direction * 2.0F +
                   glm::vec3{0, std::tan(glm::radians(angles_.y)) * 2.0F, 0});
}

} // namespace lc1
