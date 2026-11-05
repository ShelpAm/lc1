#pragma once

#include "lc1/game/character/character-body.hpp"
#include "lc1/scene/transform.hpp"

namespace lc1 {
class Character;

// A value copy of gameplay pose, safe to pass to the render thread.
struct CharacterPose {
    scene::Transform world_transform;
    CharacterBody body;
    float relative_look_yaw = 0;
    float relative_look_pitch = 0;

    CharacterPose() = default;
    CharacterPose(Character const &character);

    glm::vec3 position() const;
    scene::Transform const &transform() const;
    float body_yaw() const;
    float look_yaw() const;
    float look_pitch() const;
    glm::vec3 heading() const;
    glm::vec3 right() const;
    glm::vec3 look_direction() const;
    glm::vec3 eye_position() const;
};
} // namespace lc1
