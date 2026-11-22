#pragma once

#include "lc1/game/character/character-controller.hpp"
#include "lc1/game/character/character-lookup.hpp"
#include "lc1/game/party/companion-order.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace lc1 {

class Character;
class DialogSystem;

struct Waypoint {
    glm::vec3 position;
    float wait_seconds = 0.0F; // Seconds to stay at this point before moving on.
    bool sprint = false;
};

class AiCharacterController : public CharacterController {
  public:
    explicit AiCharacterController(DialogSystem const &dialogs, CharacterLookup characters);
    explicit AiCharacterController(DialogSystem const &dialogs, CharacterLookup characters,
                                   std::span<Character *const> action_targets,
                                   std::vector<Waypoint> path, float arrival_radius = 0.5F,
                                   std::optional<CharacterId> ambient_patrol = std::nullopt);
    void set_order(CompanionOrder order);

    CharacterIntent decide(float delta_seconds) override;
    bool patrolling() const;
    std::optional<CompanionOrder> companion_order() const;

  private:
    enum class State : std::uint8_t { Idle, Patrol, Follow, Wait, Dialog };

    CharacterIntent patrol_update(float delta_seconds, Character const &character);
    CharacterIntent companion_update(float delta_seconds, Character const &character);

    // Required read-only borrow; dialogs must outlive this controller.
    DialogSystem const *dialogs_;
    CharacterLookup characters_;
    // Optional read-only borrows; supplied demo targets outlive this controller.
    std::vector<Character *> action_targets_;
    State state_ = State::Idle;
    State resume_state_ = State::Idle;
    std::vector<Waypoint> path_;
    std::size_t current_target_ = 0;
    float arrival_radius_ = 0.5F;
    float wait_remaining_ = 0.0F;
    std::optional<CharacterId> ambient_patrol_;
};

} // namespace lc1
