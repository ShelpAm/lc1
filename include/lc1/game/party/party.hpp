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
    Party(std::uint32_t keyid, Faction *faction, Character *leader = nullptr,
          std::vector<Character *> members = {}, std::vector<Character *> captives = {});

    std::uint32_t keyid() const;

    Faction *faction();

    Faction const *faction() const;

    Character *leader() const;

    std::vector<Character *> const &members() const;

    std::vector<Character *> const &captives() const;

    // Load-time wiring. A party record lists character keyids; the characters
    // themselves only exist after every party has been created.
    void set_leader(Character *leader);

    // Members exclude the separately stored leader; neither list may overlap the other.
    void add_member(Character *member);

    void add_captive(Character *captive);

    // Only the leader commands ordinary members. Captives and other parties are excluded.
    bool can_command(Character const &commander, Character const &member) const;
    void command(Character const &commander, Character &member, CompanionOrder order);

    bool can_recruit(Character const &recruiter, Character const &candidate) const;
    // Requires can_recruit(). Transfers membership and releases our own captives.
    // All borrowed characters and their parties must outlive this operation.
    void recruit(Character &recruiter, Character &candidate);

  private:
    std::uint32_t keyid_;
    Faction *faction_ = nullptr;
    Character *leader_ = nullptr;
    std::vector<Character *> members_;
    std::vector<Character *> captives_;
};

} // namespace lc1
