#pragma once

#include "lc1/game/character/character-lookup.hpp"
#include "lc1/game/party/companion-order.hpp"
#include <optional>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lc1 {

class Character;

// A party is a container and a map entity, not a pile of stats: it names concrete
// characters by ID and owns no simulation objects. Snapshots copy the same value type.
// The leader is optional because assembly happens after all characters exist.
class Party {
  public:
    Party(std::uint32_t keyid, std::uint32_t faction);

    // One-time assembly. Every non-null character must already name this party.
    // Validates before committing, then retains IDs only.
    void restore_roster(Character *leader, std::vector<Character *> members,
                        std::vector<Character *> captives);

    // Promote an ordinary member (or clear leadership), demoting the previous leader.
    void change_leader(CharacterLookup const &characters, std::optional<CharacterId> leader);

    std::uint32_t keyid() const;
    std::uint32_t faction() const;
    std::optional<CharacterId> leader() const;
    std::vector<CharacterId> const &members() const;
    std::vector<CharacterId> const &captives() const;

    // Ordinary members only: the leader and captives have separate roles.
    bool has_member(CharacterId character) const;
    bool has_captive(CharacterId character) const;

    // Only the leader commands AI-controlled ordinary members. Captives and other parties are
    // excluded.
    bool can_command(Character const &commander, Character const &member) const;
    void command(Character const &commander, Character &member, CompanionOrder order);

    bool can_recruit(Character const &recruiter, Character const &candidate) const;
    // Requires can_recruit(). Transfers membership and releases our own captives.
    // Human controls are preserved; AI recruits follow the new leader.
    // All borrowed characters and their parties must outlive this operation.
    void recruit(Character &recruiter, Character &candidate);

  private:
    bool roster_restored_ = false;
    std::uint32_t keyid_;
    std::uint32_t faction_;
    std::optional<CharacterId> leader_;
    std::vector<CharacterId> members_;
    std::vector<CharacterId> captives_;
};

} // namespace lc1
