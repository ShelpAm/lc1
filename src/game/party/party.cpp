#include "lc1/game/party/party.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character.hpp"

#include <algorithm>
#include <memory>

namespace lc1 {

Party::Party(std::uint32_t keyid, Faction *faction) : keyid_(keyid), faction_(faction)
{
}

void Party::restore_roster(Character *leader, std::vector<Character *> members,
                           std::vector<Character *> captives)
{
    if (roster_restored_)
        fail("party {} roster is already restored", keyid_);
    std::vector<Character *> seen;
    auto check = [&](Character *character) {
        if (!character || character->config().party != this ||
            std::ranges::contains(seen, character))
            fail("party {} roster requires unique characters belonging to this party", keyid_);
        seen.push_back(character);
    };
    if (leader)
        check(leader);
    for (auto *member : members)
        check(member);
    for (auto *captive : captives)
        check(captive);
    leader_ = leader;
    members_ = std::move(members);
    captives_ = std::move(captives);
    roster_restored_ = true;
}

void Party::change_leader(Character *leader)
{
    if (!roster_restored_)
        fail("party {} roster is not restored", keyid_);
    if (leader == leader_)
        return;
    if (leader && (leader->config().party != this || !std::ranges::contains(members_, leader)))
        fail("party {} new leader must be an ordinary member", keyid_);
    // Allocate before changing either role, preserving the roster on failure.
    if (leader_)
        members_.push_back(leader_);
    if (leader) {
        std::erase(members_, leader);
        // A leader cannot keep a companion order targeting itself.
        if (leader->companion_order() != CompanionOrder::None)
            leader->set_controller(nullptr);
    }
    leader_ = leader;
    if (!leader_)
        for (auto *member : members_)
            if (member->companion_order() != CompanionOrder::None)
                member->set_controller(nullptr);
}

bool Party::can_command(Character const &commander, Character const &member) const
{
    return leader_ == &commander && commander.config().party == this &&
           member.config().party == this && &commander != &member &&
           std::ranges::contains(members_, &member) && !std::ranges::contains(captives_, &member);
}

void Party::command(Character const &commander, Character &member, CompanionOrder order)
{
    if (!can_command(commander, member) ||
        (order != CompanionOrder::Follow && order != CompanionOrder::Wait))
        fail("character {} cannot command character {} in party {}", commander.config().keyid,
             member.config().keyid, keyid_);
    // The order is controller-owned behavior state: install a companion controller
    // instead of keeping a second mode copy on Character.
    auto controller = std::make_unique<AiCharacterController>(order);
    member.set_controller(std::move(controller));
}

bool Party::can_recruit(Character const &recruiter, Character const &candidate) const
{
    auto const *source = candidate.config().party;
    if (!roster_restored_ || !source || !source->roster_restored_ || &recruiter == &candidate ||
        recruiter.config().party != this || leader_ != &recruiter ||
        source->leader_ == &candidate || std::ranges::contains(members_, &candidate))
        return false;
    if (source == this)
        return std::ranges::contains(captives_, &candidate);
    return std::ranges::contains(source->members_, &candidate) &&
           !std::ranges::contains(source->captives_, &candidate) &&
           !std::ranges::contains(captives_, &candidate);
}

void Party::recruit(Character &recruiter, Character &candidate)
{
    if (!can_recruit(recruiter, candidate))
        fail("character {} cannot recruit character {} into party {}", recruiter.config().keyid,
             candidate.config().keyid, keyid_);
    auto *source = candidate.config_.party;
    auto controller = std::make_unique<AiCharacterController>(CompanionOrder::Follow);
    // Allocate before mutating the source so allocation failure preserves membership.
    members_.push_back(&candidate);
    if (source != this)
        std::erase(source->members_, &candidate);
    std::erase(source->captives_, &candidate);
    candidate.config_.party = this;
    // Newly recruited members follow their new leader; the previous patrol is discarded.
    candidate.set_controller(std::move(controller));
}

} // namespace lc1

namespace lc1 {

std::uint32_t Party::keyid() const
{
    return keyid_;
}

Faction *Party::faction()
{
    return faction_;
}

Faction const *Party::faction() const
{
    return faction_;
}

Character *Party::leader() const
{
    return leader_;
}

std::vector<Character *> const &Party::members() const
{
    return members_;
}

std::vector<Character *> const &Party::captives() const
{
    return captives_;
}

} // namespace lc1
