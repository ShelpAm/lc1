#pragma once

#include <cstddef>

namespace lc1 {

enum class CharacterAnimation {
    Idle,
    Walk,
    Sprint,
    Attack,
    HitReact,
    Death,
    WalkStart,
    SprintStart,
    WalkStopLeft,
    WalkStopRight,
    SprintStopLeft,
    SprintStopRight,
    WalkToSprint,
    SprintToWalk,
    TurnLeft90,
    TurnRight90,
    TurnLeft180,
    TurnRight180,
    WalkTurnLeft,
    WalkTurnRight,
    SprintTurnLeft,
    SprintTurnRight,
    Action,
    Count,
};

inline constexpr auto character_animation_count =
    static_cast<std::size_t>(CharacterAnimation::Count);

} // namespace lc1
