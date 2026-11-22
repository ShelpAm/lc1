#include "lc1/game/camera/first-person-camera-controller.hpp"

#include "lc1/game/character/character-pose.hpp"
#include "lc1/scene/camera.hpp"

namespace lc1 {

void FirstPersonCameraController::remember_angles(CharacterPose const &player)
{
    remembered_angles_ = {player.body_yaw() + player.look_yaw(), player.look_pitch()};
}

glm::vec2 FirstPersonCameraController::remembered_angles() const
{
    return remembered_angles_;
}

void FirstPersonCameraController::update(scene::FpsCamera &camera, CharacterPose const &player,
                                         std::optional<glm::mat4> const &eye_transform) const
{
    auto direction = player.look_direction();
    auto position = player.eye_position();
    if (eye_transform) {
        position = glm::vec3{(*eye_transform)[3]};
        // The mount already includes body yaw; apply gameplay look in body-local space.
        auto const local_look =
            glm::inverse(glm::mat3{player.transform().model_matrix()}) * direction;
        direction = glm::normalize(glm::mat3{*eye_transform} * local_look);
    }
    camera.set_position(position);
    camera.look_at(position + direction);
}

} // namespace lc1
