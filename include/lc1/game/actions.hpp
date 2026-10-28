#pragma once

#include "lc1/input.hpp"

#include <cstddef>
#include <cstdint>
#include <ranges>
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

template <typename ActionEnum, typename ContextEnum>
inline int response_index(BasicInputRouter<ActionEnum, ContextEnum> const &input_router)
{
    constexpr std::array actions{GameAction::DialogResponse0, GameAction::DialogResponse1,
                                 GameAction::DialogResponse2, GameAction::DialogResponse3};
    for (auto const [index, action] : std::ranges::views::zip(std::views::iota(0), actions)) {
        if (input_router.pressed(action))
            return index;
    }
    return -1;
}

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
