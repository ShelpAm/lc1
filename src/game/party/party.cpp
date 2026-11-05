#include "lc1/game/party/party.hpp"

#include "lc1/error.hpp"
#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character.hpp"

#include <algorithm>

namespace lc1 {

Party::Party(std::uint32_t keyid, std::uint32_t faction) : keyid_(keyid), faction_(faction)
{
}

void Party::restore_roster(Character *leader, std::vector<Character *> members,
                           std::vector<Character *> captives)
{
    if (roster_restored_)
        fail("party {} roster is already restored", keyid_);
    std::vector<CharacterId> seen;
    auto check = [&](Character *character) {
        if (!character || character->config().party != this ||
            std::ranges::contains(seen, character->id()))
            fail("party {} roster requires unique characters belonging to this party", keyid_);
        seen.push_back(character->id());
    };
    if (leader)
        check(leader);
    for (auto *member : members)
        check(member);
    for (auto *captive : captives)
        check(captive);
    std::vector<CharacterId> member_ids;
    std::vector<CharacterId> captive_ids;
    member_ids.reserve(members.size());
    captive_ids.reserve(captives.size());
    for (auto const *member : members)
        member_ids.push_back(member->id());
    for (auto const *captive : captives)
        captive_ids.push_back(captive->id());
    leader_ = leader ? std::optional{leader->id()} : std::nullopt;
    members_ = std::move(member_ids);
    captives_ = std::move(captive_ids);
    roster_restored_ = true;
}

void Party::change_leader(CharacterLookup const &characters, std::optional<CharacterId> leader)
{
    if (!roster_restored_)
        fail("party {} roster is not restored", keyid_);
    if (leader == leader_)
        return;
    if (leader && (characters(*leader).config().party != this || !has_member(*leader)))
        fail("party {} new leader must be an ordinary member", keyid_);
    // Allocate before changing either role, preserving the roster on failure.
    if (leader_)
        members_.push_back(*leader_);
    if (leader) {
        std::erase(members_, *leader);
        // A leader cannot keep a companion order targeting itself.
        auto &character = characters(*leader);
        if (character.companion_order() != CompanionOrder::None)
            character.ai_controller()->set_order(CompanionOrder::None);
    }
    leader_ = leader;
    if (!leader_)
        for (auto id : members_) {
            auto &member = characters(id);
            if (member.companion_order() != CompanionOrder::None)
                member.ai_controller()->set_order(CompanionOrder::None);
        }
}

bool Party::has_member(CharacterId character) const
{
    return std::ranges::contains(members_, character);
}

bool Party::has_captive(CharacterId character) const
{
    return std::ranges::contains(captives_, character);
}

bool Party::can_command(Character const &commander, Character const &member) const
{
    return leader_ == commander.id() && commander.config().party == this &&
           member.config().party == this && &commander != &member && has_member(member.id()) &&
           !has_captive(member.id()) && member.ai_controller();
}

void Party::command(Character const &commander, Character &member, CompanionOrder order)
{
    if (!can_command(commander, member) ||
        (order != CompanionOrder::Follow && order != CompanionOrder::Wait))
        fail("character {} cannot command character {} in party {}", commander.config().keyid,
             member.config().keyid, keyid_);
    member.ai_controller()->set_order(order);
}

bool Party::can_recruit(Character const &recruiter, Character const &candidate) const
{
    auto const *source = candidate.config().party;
    if (!roster_restored_ || !source || !source->roster_restored_ || &recruiter == &candidate ||
        recruiter.config().party != this || leader_ != recruiter.id() ||
        source->leader_ == candidate.id() || has_member(candidate.id()))
        return false;
    if (source == this)
        return has_captive(candidate.id());
    return source->has_member(candidate.id()) && !source->has_captive(candidate.id()) &&
           !has_captive(candidate.id());
}

void Party::recruit(Character &recruiter, Character &candidate)
{
    if (!can_recruit(recruiter, candidate))
        fail("character {} cannot recruit character {} into party {}", recruiter.config().keyid,
             candidate.config().keyid, keyid_);
    auto *source = candidate.config_.party;
    auto *ai = candidate.ai_controller();
    // Allocate before mutating the source so allocation failure preserves membership.
    members_.push_back(candidate.id());
    if (source != this)
        std::erase(source->members_, candidate.id());
    std::erase(source->captives_, candidate.id());
    candidate.config_.party = this;
    // AI recruits follow the new leader; human recruits keep their input controller.
    if (ai)
        ai->set_order(CompanionOrder::Follow);
}

} // namespace lc1

namespace lc1 {

std::uint32_t Party::keyid() const
{
    return keyid_;
}

std::uint32_t Party::faction() const
{
    return faction_;
}

std::optional<CharacterId> Party::leader() const
{
    return leader_;
}

std::vector<CharacterId> const &Party::members() const
{
    return members_;
}

std::vector<CharacterId> const &Party::captives() const
{
    return captives_;
}

} // namespace lc1
