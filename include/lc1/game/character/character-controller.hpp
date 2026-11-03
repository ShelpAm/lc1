#pragma once

#include "lc1/game/character/character-intent.hpp"

namespace lc1 {

class Character;

// Shared intent-producing interface for AI and player controllers.
class CharacterController {
  public:
    CharacterController() = default;
    CharacterController(CharacterController const &) = delete;
    CharacterController(CharacterController &&) = delete;
    CharacterController &operator=(CharacterController const &) = delete;
    CharacterController &operator=(CharacterController &&) = delete;
    virtual ~CharacterController();

    virtual CharacterIntent update(float delta_seconds, Character const &character) = 0;
};

} // namespace lc1
