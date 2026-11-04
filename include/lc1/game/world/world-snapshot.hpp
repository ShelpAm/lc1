#pragma once

#include "lc1/game/character/bag.hpp"
#include "lc1/game/character/character-pose.hpp"
#include "lc1/game/character/model-config.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lc1 {
class GameWorld;

struct CharacterSnapshot {
    std::uint32_t keyid;
    std::string name;
    std::string faction;
    CharacterPose pose;
    ModelConfig model;
    std::optional<ModelConfig> weapon_model;
    std::uint64_t attack_sequence;
    // Ambient speech only; the player's conversation is exposed separately.
    std::string speech;
};

struct DialogSnapshot {
    std::string speaker;
    std::string speech;
    std::vector<std::string> responses;
};

// Owned values only. No character, controller, party, dialog or scene borrows.
struct WorldSnapshot {
    std::uint64_t tick = 0;
    std::uint64_t input_sequence = 0;
    std::uint64_t input_tick = 0;
    std::size_t player_index = 0;
    std::vector<CharacterSnapshot> characters;
    game::Bag bag{0};
    std::size_t companion_count = 0;
    std::string interaction_target;
    std::optional<DialogSnapshot> dialog;

    CharacterSnapshot const &player() const;
};

WorldSnapshot capture_world(GameWorld const &world);
} // namespace lc1
