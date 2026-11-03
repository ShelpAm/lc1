#pragma once

#include "lc1/game/player/player-view.hpp"

inline lc1::CharacterIntent resolve_player_input(lc1::PlayerView &view,
                                                 lc1::Character const &character,
                                                 lc1::GameInputRouter const &input,
                                                 glm::vec2 look_delta, bool keyboard_enabled,
                                                 bool mouse_enabled)
{
    view.prepare_input(look_delta, keyboard_enabled, mouse_enabled);
    lc1::PlayerCharacterController controller{input, view.character_input()};
    lc1::CharacterController &base = controller;
    return base.update(0, character);
}
