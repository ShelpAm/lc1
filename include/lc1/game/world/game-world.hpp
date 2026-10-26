#pragma once

#include "lc1/game/character/character.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/dialog/dialog.hpp"
#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/session/save.hpp"

#include <memory>
#include <span>
#include <unordered_map>

namespace lc1 {

struct WorldInteraction {
    // What the player controller asked for this frame. GameWorld applies it through
    // Character, then uses interact/interaction_direction for the world-level action.
    CharacterIntent player_intent{};
    // False while UI, focus or ImGui capture owns the input. This gates player-initiated
    // conversations; whether the character is otherwise idle is a Character rule.
    bool player_input_enabled = false;
    int response_index = -1;
};

// Owns the local simulation. No window, renderer, audio or input-device dependencies.
// Not an authoritative server or a network replica. The roster is fixed between loads.
class GameWorld {
  public:
    GameWorld(Save const &save,
              std::filesystem::path const &dialog_catalog = "runtime/dialogs/catalog.yaml");
    GameWorld(GameWorld const &) = delete;
    GameWorld &operator=(GameWorld const &) = delete;
    GameWorld(GameWorld &&) = delete;
    GameWorld &operator=(GameWorld &&) = delete;
    ~GameWorld() = default;

    Continent const &continent() const;

    Character &player();

    Character const &player() const;

    // Stable, keyid-ordered borrows; character borrows remain valid until load() or destruction.
    std::vector<Character const *> const &characters() const;

    DialogSystem const &dialogs() const;

    Character const *interaction_target() const;

    // Applies player controls, then physics and interactions; update presentation afterward.
    // Both physics and conversations advance by the same capped simulation time.
    void update(float delta_seconds, WorldInteraction const &interaction);

    // Discards runtime state and rebuilds from save, **keeping the continent and dialog catalog**.
    // Rejects a different continent identity before clearing existing state.
    // Invalidates all character/dialog borrows. If rebuilding throws, retry load() before use.
    void load(Save const &save);

    SerializedWorld snapshot() const;

  private:
    void build_world(Save const &save);
    void link_party_members(Save const &save);
    Character *character_by_id(std::uint32_t keyid, std::string_view what) const;
    Character *find_interaction_target(glm::vec3 forward) const;

    std::unordered_map<std::uint32_t, game::Resource> resources_;

    Continent continent_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Faction>> factions_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Equipment>> equipments_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Weapon>> weapons_;
    // Interactions borrow dialogs_; clear active sessions before destroying characters.
    DialogSystem dialogs_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Character>> characters_;
    std::unordered_map<std::uint32_t, std::unique_ptr<Party>> parties_;
    std::vector<Character *> dialog_participants_;
    std::vector<Character const *> visible_characters_;
    Character *player_ = nullptr;
    Character *example_ = nullptr;
    Character *interaction_target_ = nullptr;
};

} // namespace lc1
