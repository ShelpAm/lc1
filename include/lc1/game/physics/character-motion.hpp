#pragma once

#include "lc1/game/character/character-body.hpp"
#include "lc1/game/physics/gravity.hpp"

#include <glm/vec3.hpp>

namespace lc1 {

class WorldCollision;

struct CharacterMotionSettings {
    float gravity = gravityf;
    float jump_height = 0.5F;
    double max_step_seconds = 1.0 / 60.0;
};

// Upright character collision and gravity. Owns motion state, never a second world pose.
// Position arguments refer to the owner's feet and are borrowed only during the call.
// Body parameters must remain valid; construction validates them through Character.
class CharacterMotion {
  public:
    CharacterMotion(WorldCollision const &collision, CharacterBody const &body, glm::vec3 &position,
                    CharacterMotionSettings settings = {});

    void request_jump();
    bool on_ground() const;

    // Desired velocity is horizontal, in metres per second, resolved by gameplay rules.
    // Consumes all finite, nonnegative time; a queued jump is consumed only once.
    void update(float delta_seconds, WorldCollision const &collision, CharacterBody const &body,
                glm::vec3 &position, glm::vec3 desired_velocity);
    void move(WorldCollision const &collision, CharacterBody const &body, glm::vec3 &position,
              glm::vec3 displacement);

  private:
    void step(float delta_seconds, WorldCollision const &collision, CharacterBody const &body,
              glm::vec3 &position, glm::vec3 desired_velocity);
    void advance_vertical(float delta_seconds, WorldCollision const &collision,
                          CharacterBody const &body, glm::vec3 &position);

    CharacterMotionSettings settings_;
    bool on_ground_ = false;
    float vertical_velocity_ = 0.0F;
    bool jump_requested_ = false;
};

} // namespace lc1
