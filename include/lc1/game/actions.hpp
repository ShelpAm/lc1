#pragma once

#include "lc1/input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lc1 {

// Gameplay action vocabulary. This is the game-side mapping layer: lc1/input.hpp owns
// raw device state and the generic BasicInputRouter, while this header names the actions
// the game asks for and the contexts in which bindings resolve.
enum class GameAction : std::uint8_t {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Sprint,
    Jump,
    Nod,
    Interact,
    ToggleCameraView,
    CycleLighting,
    ToggleShowcaseMotion,
    IncreaseExposure,
    DecreaseExposure,
    ToggleCameraLock,
    RecenterCamera,
    DialogResponse0,
    DialogResponse1,
    DialogResponse2,
    DialogResponse3,
    SaveGame,
    LoadGame,
    QuitGame,
    Count,
};

inline constexpr std::size_t game_action_count = static_cast<std::size_t>(GameAction::Count);

// Which set of gameplay bindings is live.
//
// Exactly one context is active at a time -- the one on top of GameInputRouter's stack --
// because switching modes has to be able to turn a whole set of controls off, and "off" is
// far easier to reason about than priority rules between overlapping layers.
enum class GameInputContext : std::uint8_t {
    // One ground-level control scheme for countryside, cities and combat.
    Gameplay,
    // Nothing binds here. Pushing it is the gate that hands the keyboard to a text field
    // or to ImGui -- every action simply resolves to false. Included from the start
    // because retrofitting it means auditing every held()/pressed() call site.
    Ui,
};

using GameBinding = BasicBinding<GameAction, GameInputContext>;
using GameInputRouter = BasicInputRouter<GameAction, GameInputContext>;

std::vector<GameBinding> default_game_bindings();

} // namespace lc1
