#pragma once

#include "lc1/game/character/character.hpp"

class TestCharacterController : public lc1::CharacterController {
  public:
    explicit TestCharacterController(lc1::CharacterIntent intent) : intent_(intent) {}

    lc1::CharacterIntent update(float, lc1::Character const &) override { return intent_; }

  private:
    lc1::CharacterIntent intent_;
};

inline void apply_character_intent(lc1::Character &character, float delta_seconds,
                                   lc1::Continent const &continent, lc1::CharacterIntent intent)
{
    character.set_controller(std::make_unique<TestCharacterController>(intent));
    character.update(delta_seconds, continent);
}
