#include "lc1/game/character/character-pose.hpp"
#include "lc1/game/character/character.hpp"

#include <cmath>

namespace lc1 {
CharacterPose::CharacterPose(Character const &character)
    : world_transform(character.transform()), body(character.body()),
      relative_look_yaw(character.look_yaw()), relative_look_pitch(character.look_pitch())
{
}

glm::vec3 CharacterPose::position() const
{
    return world_transform.position;
}

scene::Transform const &CharacterPose::transform() const
{
    return world_transform;
}

float CharacterPose::body_yaw() const
{
    return world_transform.rotation.y;
}

float CharacterPose::look_yaw() const
{
    return relative_look_yaw;
}

float CharacterPose::look_pitch() const
{
    return relative_look_pitch;
}

glm::vec3 CharacterPose::heading() const
{
    auto const angle = glm::radians(body_yaw() + look_yaw());
    return {-std::sin(angle), 0, -std::cos(angle)};
}

glm::vec3 CharacterPose::right() const
{
    return glm::cross(heading(), glm::vec3{0, 1, 0});
}

glm::vec3 CharacterPose::look_direction() const
{
    auto const pitch = glm::radians(look_pitch());
    return heading() * std::cos(pitch) + glm::vec3{0, std::sin(pitch), 0};
}

glm::vec3 CharacterPose::eye_position() const
{
    return position() + glm::vec3{0, body.eye_height, 0};
}
} // namespace lc1
