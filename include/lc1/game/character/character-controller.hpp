#pragma once

#include "lc1/game/character/character-intent.hpp"
#include "lc1/game/party/companion-order.hpp"

#include <optional>

namespace lc1 {

class Character;

// Controllers decide what a character wants to do; they do not move it, alter its
// velocity, clamp its speed or reject actions against its state. Every policy that
// remains true when the player is replaced by an AI belongs to Character (or another
// character gameplay subsystem), not to this interface.
class CharacterController {
  public:
    CharacterController() = default;
    CharacterController(CharacterController const &) = delete;
    CharacterController(CharacterController &&) = delete;
    CharacterController &operator=(CharacterController const &) = delete;
    CharacterController &operator=(CharacterController &&) = delete;
    virtual ~CharacterController() = default;

    // An interface, not a default: a controller that does nothing would silently stop
    // driving its character. The caller passes the same finite, 0.1 s-capped simulation
    // time it will pass to Character::update(); the controller returns what it wants.
    virtual CharacterIntent update(float delta_seconds, Character const &character) = 0;

    // Companion order is controller-owned behavior state. A patrol or other non-companion
    // controller reports std::nullopt; Character maps that to CompanionOrder::None.
    virtual std::optional<CompanionOrder> companion_order() const;
};

} // namespace lc1
