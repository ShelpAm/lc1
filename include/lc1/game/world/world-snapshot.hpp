#pragma once

#include "lc1/game/character/bag.hpp"
#include "lc1/game/character/character-pose.hpp"
#include "lc1/game/character/model-config.hpp"
#include "lc1/game/player/player-id.hpp"

#include <cstdint>
#include <map>
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
    std::uint64_t nod_sequence;
    // Full world speech; each observer decides whether to show it overhead or in the HUD.
    std::string speech;
    std::optional<std::uint32_t> dialog_partner;
};

struct DialogSnapshot {
    std::string speaker;
    std::string speech;
    std::vector<std::string> responses;
};

struct PlayerSnapshot {
    std::uint32_t character_id;
    std::uint64_t input_sequence = 0;
    std::uint64_t input_tick = 0;
    game::Bag bag{0};
    std::size_t companion_count = 0;
    std::string interaction_target;
    std::optional<DialogSnapshot> dialog;
};

// Owned values only. Public character state and per-player state have separate identities.
// This local exchange is not a network replication or visibility policy.
struct WorldSnapshot {
    uint64_t ab;
    std::uint64_t tick = 0;
    std::vector<CharacterSnapshot> characters;
    std::map<PlayerId, PlayerSnapshot> players;

    CharacterSnapshot const &character(std::uint32_t id) const;
    CharacterSnapshot const &player(PlayerId id) const;
};

// Presentation only: interpolate poses of the fixed, keyid-sorted character roster.
// Discrete state and tick acknowledgements come from current. Alpha is in [0, 1].
WorldSnapshot interpolate_world(WorldSnapshot const &previous, WorldSnapshot const &current,
                                float alpha);

WorldSnapshot capture_world(GameWorld const &world);
} // namespace lc1
