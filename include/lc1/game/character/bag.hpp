#pragma once

#include "lc1/game/resources/resource.hpp"

#include <limits>
#include <unordered_map>

namespace lc1::game {

struct Recipe;

class Bag {
    friend bool craft(Bag &bag, Recipe const &recipe);

  public:
    explicit Bag(std::uint32_t max_weight);

    // Weights and capacity are in grams. An entry stores only its keyid and quantity, so the
    // definition is borrowed for the call, not for the entry's lifetime. Definitions sharing a
    // keyid must agree on weight, because removal weighs what the caller passes. Returns false
    // for insufficient capacity.
    bool add(Resource const &resource, std::uint32_t quantity);

    bool remove(Resource const &resource, std::uint32_t quantity);

    std::uint32_t quantity_of(std::uint32_t keyid) const;

    // Nonempty entries, keyid to quantity, in unspecified order.
    std::unordered_map<std::uint32_t, std::uint32_t> const &quantities() const;

    std::uint32_t total_weight() const;

    std::uint32_t max_weight() const;

  private:
    std::unordered_map<std::uint32_t, std::uint32_t> quantities_;
    std::uint32_t total_weight_ = 0; // gram(s)
    std::uint32_t max_weight_;
};

} // namespace lc1::game
