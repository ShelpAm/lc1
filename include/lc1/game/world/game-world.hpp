#pragma once

#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/dialog/dialog.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/player/player-character-controller.hpp"
#include "lc1/game/session/save.hpp"

#include <map>
#include <memory>
#include <span>
#include <unordered_map>

namespace lc1 {

// Explicit scenario inputs, independent of presentation and device resources.
struct PatrolSetup {
    std::vector<Waypoint> route;
    std::optional<glm::vec3> spawn;
    float arrival_radius = 0.5F;
    bool resume_nearest = false;
};

struct WorldSetup {
    Continent continent;
    std::optional<PatrolSetup> example_patrol;
    std::vector<SerializedResourceStack> initial_player_resources;
};

struct WorldPlayer {
    Character *character; // Non-null; owned by the world.
    PlayerControls controls;
    Character *interaction_target = nullptr;
};

// Owns the local simulation. No window, renderer, audio or input-device dependencies.
// Not an authoritative server or a network replica. The roster is fixed between loads.
class GameWorld {
  public:
    explicit GameWorld(Save const &save, std::filesystem::path const &dialog_catalog);

    GameWorld(GameWorld const &) = delete;
    GameWorld &operator=(GameWorld const &) = delete;
    GameWorld(GameWorld &&) = delete;
    GameWorld &operator=(GameWorld &&) = delete;

    ~GameWorld() = default;

    Continent const &continent() const;
    Character &character(CharacterId id);
    Character const &character(CharacterId id) const;
    Faction const &faction(std::uint32_t id) const;

    Character &player(PlayerId id);
    Character const &player(PlayerId id) const;

    std::map<PlayerId, WorldPlayer> const &players() const;

    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> &parties();
    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> const &parties() const;

    // Stable, keyid-ordered borrows; character borrows remain valid until load() or destruction.
    std::span<Character *const> characters();
    std::vector<Character const *> const &characters() const;

    DialogSystem const &dialogs() const;

    Character const *interaction_target(PlayerId id) const;

    // Applies player controls, then physics and interactions; update presentation afterward.
    // Both physics and conversations advance by the same full simulation time.
    void update(float delta_seconds, std::map<PlayerId, PlayerControls> const &inputs);

    // Discards runtime state and rebuilds from save, **keeping the continent and dialog catalog**.
    // Rejects a different continent identity before clearing existing state.
    // Invalidates all character/dialog borrows. If rebuilding throws, retry load() before use.
    void load(Save const &save);

    SerializedWorld snapshot() const;

  private:
    void build_world(Save const &save);
    void link_party_members(Save const &save);
    Character *character_by_id(std::uint32_t keyid, std::string_view what) const;
    Character *find_interaction_target(Character &actor) const;

    WorldSetup setup_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Faction>> factions_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Equipment>> equipments_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Weapon>> weapons_;
    // Interactions borrow dialogs_; clear active sessions before destroying characters.
    DialogSystem dialogs_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Character>> characters_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> parties_;
    std::vector<Character *> dialog_participants_;
    std::vector<Character const *> visible_characters_;
    // Stable map nodes own the values borrowed by player controllers.
    std::map<PlayerId, WorldPlayer> players_;
    Character *example_ = nullptr;
};

} // namespace lc1
