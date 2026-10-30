#pragma once

#include "lc1/game/party/companion-order.hpp"

#include <cstdint>
#include <vector>

namespace lc1 {

class Character;
class Faction;

// A party is a container and a map entity, not a pile of stats: it names concrete
// characters and owns none of them. The leader is optional because a save is loaded in
// two phases -- parties first, membership once every character exists.
class Party {
  public:
    Party(std::uint32_t keyid, Faction *faction);
    Party(Party const &) = delete;
    Party &operator=(Party const &) = delete;
    Party(Party &&) = delete;
    Party &operator=(Party &&) = delete;

    // One-time assembly. Every non-null character must already name this party.
    // Validates before committing; characters must remain alive while the roster is used.
    void restore_roster(Character *leader, std::vector<Character *> members,
                        std::vector<Character *> captives);

    // Promote an ordinary member (or clear leadership), demoting the previous leader.
    void change_leader(Character *leader);

    std::uint32_t keyid() const;

    Faction *faction();

    Faction const *faction() const;

    Character *leader() const;

    std::vector<Character *> const &members() const;

    std::vector<Character *> const &captives() const;

    // Only the leader commands ordinary members. Captives and other parties are excluded.
    bool can_command(Character const &commander, Character const &member) const;
    void command(Character const &commander, Character &member, CompanionOrder order);

    bool can_recruit(Character const &recruiter, Character const &candidate) const;
    // Requires can_recruit(). Transfers membership and releases our own captives.
    // All borrowed characters and their parties must outlive this operation.
    void recruit(Character &recruiter, Character &candidate);

  private:
    bool roster_restored_ = false;
    std::uint32_t keyid_;
    Faction *faction_ = nullptr;
    Character *leader_ = nullptr;
    std::vector<Character *> members_;
    std::vector<Character *> captives_;
};

} // namespace lc1
