#include "lc1/game/party/party.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character.hpp"

#include <algorithm>
#include <memory>

namespace lc1 {

Party::Party(std::uint32_t keyid, Faction *faction, Character *leader,
             std::vector<Character *> members, std::vector<Character *> captives)
    : keyid_(keyid), faction_(faction), leader_(leader)
{
    for (auto *member : members)
        add_member(member);
    for (auto *captive : captives)
        add_captive(captive);
}

void Party::set_leader(Character *leader)
{
    if (leader &&
        (std::ranges::contains(members_, leader) || std::ranges::contains(captives_, leader)))
        fail("party {} leader must not appear in members or captives", keyid_);
    leader_ = leader;
}

void Party::add_member(Character *member)
{
    if (!member || member == leader_ || std::ranges::contains(members_, member) ||
        std::ranges::contains(captives_, member))
        fail("party {} needs a unique ordinary member", keyid_);
    members_.push_back(member);
}

void Party::add_captive(Character *captive)
{
    if (!captive || captive == leader_ || std::ranges::contains(members_, captive) ||
        std::ranges::contains(captives_, captive))
        fail("party {} needs a unique captive", keyid_);
    captives_.push_back(captive);
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
    if (&recruiter == &candidate || recruiter.config().party != this || leader_ != &recruiter ||
        source == nullptr || source->leader_ == &candidate ||
        std::ranges::contains(members_, &candidate))
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
    // Allocate before mutating the source so allocation failure preserves membership.
    members_.push_back(&candidate);
    if (source != this)
        std::erase(source->members_, &candidate);
    std::erase(source->captives_, &candidate);
    candidate.config_.party = this;
    // Newly recruited members follow their new leader; the previous patrol is discarded.
    command(recruiter, candidate, CompanionOrder::Follow);
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
