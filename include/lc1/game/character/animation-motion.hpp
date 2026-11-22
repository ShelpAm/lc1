#pragma once

#include "lc1/game/character/character-animation.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace lc1 {

struct ModelConfig;

struct TimedCharacterAnimation {
    CharacterAnimation clip = CharacterAnimation::Idle;
    float seconds = 0;
};

struct AnimationMotionClip {
    std::string name;
    float duration = 0;
    float speed_from = 0;
    float speed_to = 0;
    CharacterAnimation next = CharacterAnimation::Idle;
    float next_phase = 0;
    float turn = 0;
};

struct AnimationMotion {
    std::array<AnimationMotionClip, character_animation_count> clips{};
    float attack_start;
    float attack_end;

    AnimationMotionClip const &operator[](CharacterAnimation action) const
    {
        return clips[static_cast<std::size_t>(action)];
    }
};

std::optional<AnimationMotion> load_animation_motion(ModelConfig const &config,
                                                     std::filesystem::path const &asset_root);

} // namespace lc1
