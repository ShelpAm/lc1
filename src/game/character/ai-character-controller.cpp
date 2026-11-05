#include "lc1/game/character/ai-character-controller.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/dialog/dialog.hpp"
#include "lc1/game/party/party.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace lc1 {

AiCharacterController::AiCharacterController(DialogSystem const &dialogs,
                                             CharacterLookup characters)
    : dialogs_(&dialogs), characters_(std::move(characters))
{
}

AiCharacterController::AiCharacterController(DialogSystem const &dialogs,
                                             CharacterLookup characters,
                                             std::span<Character *const> action_targets,
                                             std::vector<Waypoint> path, float arrival_radius)
    : dialogs_(&dialogs), characters_(std::move(characters)),
      action_targets_(action_targets.begin(), action_targets.end()), state_(State::Patrol),
      path_(std::move(path)), arrival_radius_(arrival_radius)
{
    assert(!path_.empty() && "path must have at least one waypoint");
    current_target_ = path_.size() > 1 ? 1 : 0;
}

void AiCharacterController::set_order(CompanionOrder order)
{
    State state;
    switch (order) {
    case CompanionOrder::None:
        state = State::Idle;
        break;
    case CompanionOrder::Follow:
        state = State::Follow;
        break;
    case CompanionOrder::Wait:
        state = State::Wait;
        break;
    default:
        fail("invalid companion order");
    }
    if (state_ == State::Dialog)
        resume_state_ = state;
    else
        state_ = state;
}

CharacterIntent AiCharacterController::update(float delta_seconds, Character const &character)
{
    auto const *dialog = dialogs_->find(character);
    if (dialog) {
        if (state_ != State::Dialog)
            resume_state_ = state_;
        state_ = State::Dialog;
    }
    else if (state_ == State::Dialog) {
        state_ = resume_state_;
    }

    CharacterIntent intent;
    switch (state_) {
    case State::Idle:
    case State::Wait:
        break;
    case State::Patrol:
        intent = patrol_update(delta_seconds, character);
        break;
    case State::Follow:
        intent = companion_update(delta_seconds, character);
        break;
    case State::Dialog:
        intent.body_direction =
            (dialog->talker() == &character ? dialog->target() : dialog->talker())->position() -
            character.position();
        return intent;
    }

    intent.swing_weapon = std::ranges::any_of(action_targets_, [&](Character const *target) {
        return glm::distance(target->position(), character.position()) <= 3.0F;
    });
    if (action_targets_.empty() && character.config().party) {
        auto const id = character.config().party->leader();
        auto const *leader = id ? &characters_(*id) : nullptr;
        intent.swing_weapon =
            leader && glm::distance(leader->position(), character.position()) <= 3.0F;
    }

    return intent;
}

bool AiCharacterController::patrolling() const
{
    return (state_ == State::Dialog ? resume_state_ : state_) == State::Patrol;
}

std::optional<CompanionOrder> AiCharacterController::companion_order() const
{
    switch (state_ == State::Dialog ? resume_state_ : state_) {
    case State::Follow:
        return CompanionOrder::Follow;
    case State::Wait:
        return CompanionOrder::Wait;
    default:
        return std::nullopt;
    }
}

CharacterIntent AiCharacterController::patrol_update(float delta_seconds,
                                                     Character const &character)
{
    CharacterIntent intent;

    if (path_.size() == 1) {
        wait_remaining_ = std::max(0.0F, wait_remaining_ - delta_seconds);
        if (wait_remaining_ == 0.0F)
            wait_remaining_ = path_[0].wait_seconds;
        return intent;
    }

    if (wait_remaining_ > 0.0F) {
        wait_remaining_ = std::max(0.0F, wait_remaining_ - delta_seconds);
        if (wait_remaining_ == 0.0F)
            current_target_ = (current_target_ + 1) % path_.size();
        return intent;
    }

    auto const target_pos = path_[current_target_].position;
    if (glm::distance(character.position(), target_pos) <= arrival_radius_) {
        wait_remaining_ = path_[current_target_].wait_seconds;
        if (wait_remaining_ == 0.0F)
            current_target_ = (current_target_ + 1) % path_.size();
        return intent;
    }

    auto const to_target = target_pos - character.position();
    if (glm::length(to_target) > 1e-12F) {
        auto const direction = glm::normalize(to_target);
        // Character owns speed, collision and body turning; the patrol only asks to move.
        intent.move = glm::vec2{direction.x, direction.z};
        intent.face_movement = true;
    }

    return intent;
}

CharacterIntent AiCharacterController::companion_update(float delta_seconds,
                                                        Character const &character)
{
    CharacterIntent intent;
    auto const *party = character.config().party;
    auto const id = party ? party->leader() : std::nullopt;
    auto const *leader = id ? &characters_(*id) : nullptr;
    if (!leader || !party->can_command(*leader, character))
        return intent;

    auto direction = leader->position() - character.position();
    direction.y = 0.0F;
    float const distance = glm::length(direction);
    float const stop_distance = std::max(2.0F, character.half_width() + leader->half_width());
    if (distance <= stop_distance || character.body().walk_speed <= 0.0F)
        return intent;

    auto const heading = direction / distance;
    // Facing the leader is a character intent, not a controller-side pose change. Keep it
    // even on a zero-time step; the movement below is limited to the remaining gap.
    intent.body_direction = heading;
    if (delta_seconds <= 0.0F)
        return intent;

    float const step_seconds =
        std::min(delta_seconds, (distance - stop_distance) / character.body().walk_speed);
    float const scale = std::clamp(step_seconds / delta_seconds, 0.0F, 1.0F);
    intent.move = glm::vec2{heading.x, heading.z} * scale;
    intent.face_movement = true;
    return intent;
}

} // namespace lc1
