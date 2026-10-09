#pragma once

#include "lc1/game/character.hpp"

#include <memory>
#include <vector>

namespace lc1 {

class Faction;

class Party {
  public:
    std::vector<DrawItem> draw_items() const
    {
        std::vector<DrawItem> items;
        items.reserve(members_.size() + 1);
        items.push_back(leader_->draw_item());
        for (auto const &member : members_) {
            items.push_back(member->draw_item());

        }

        return items;
    }
    Faction *faction() { return faction_; }

  private:
    Faction *faction_ = nullptr;
    Character *leader_ = nullptr;
    std::vector<std::unique_ptr<Character>> members_;

};

} // namespace lc1
