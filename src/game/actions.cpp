#include "lc1/game/actions.hpp"
#include <ranges>

namespace lc1 {

std::vector<GameBinding> default_game_bindings()
{
    std::vector<GameBinding> bindings{
        // NOLINTBEGIN
        {GameInputContext::Gameplay, GameAction::MoveForward, Key::W},
        {GameInputContext::Gameplay, GameAction::MoveForward, Key::Up},
        {GameInputContext::Gameplay, GameAction::MoveBackward, Key::S},
        {GameInputContext::Gameplay, GameAction::MoveBackward, Key::Down},
        {GameInputContext::Gameplay, GameAction::MoveLeft, Key::A},
        {GameInputContext::Gameplay, GameAction::MoveLeft, Key::Left},
        {GameInputContext::Gameplay, GameAction::MoveRight, Key::D},
        {GameInputContext::Gameplay, GameAction::MoveRight, Key::Right},
        {GameInputContext::Gameplay, GameAction::Sprint, Key::LeftShift},
        {GameInputContext::Gameplay, GameAction::Jump, Key::Space},
        {GameInputContext::Gameplay, GameAction::Nod, Key::F2},
        {GameInputContext::Gameplay, GameAction::Interact, Key::F},
        {GameInputContext::Gameplay, GameAction::DialogResponse0, Key::Digit1},
        {GameInputContext::Gameplay, GameAction::DialogResponse1, Key::Digit2},
        {GameInputContext::Gameplay, GameAction::DialogResponse2, Key::Digit3},
        {GameInputContext::Gameplay, GameAction::DialogResponse3, Key::Digit4},
        {GameInputContext::Gameplay, GameAction::SaveGame, Key::F6},
        {GameInputContext::Gameplay, GameAction::LoadGame, Key::F9},
        {GameInputContext::Gameplay, GameAction::QuitGame, Key::Escape},
        {GameInputContext::Gameplay, GameAction::ToggleCameraView, Key::M},
        {GameInputContext::Gameplay, GameAction::ToggleCameraLock, Key::Y},
        {GameInputContext::Gameplay, GameAction::RecenterCamera, Key::Space},
        {GameInputContext::Gameplay, GameAction::CycleLighting, Key::F4},
        {GameInputContext::Gameplay, GameAction::ToggleShowcaseMotion, Key::F5},
        {GameInputContext::Gameplay, GameAction::IncreaseExposure, Key::PageUp},
        {GameInputContext::Gameplay, GameAction::DecreaseExposure, Key::PageDown},
        // NOLINTEND
    };
    return bindings;
}

} // namespace lc1
