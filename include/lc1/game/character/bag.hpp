#pragma once

#include "lc1/game/resources/resource-stack.hpp"

#include <limits>
#include <ranges>
#include <unordered_map>

namespace lc1::game {

struct Recipe;

class Bag {
    friend bool craft(Bag &bag, Recipe const &recipe);

  public:
    explicit Bag(std::uint32_t max_weight);

    // Borrows a non-null definition that must outlive this bag. Weights and capacity are in grams.
    // Returns false for insufficient capacity.
    bool add(Resource const &resource, std::uint32_t quantity);

    bool remove(Resource const &resource, std::uint32_t quantity);

    std::uint32_t quantity_of(std::uint32_t keyid) const;

    std::ranges::values_view<
        std::ranges::ref_view<std::unordered_map<std::uint32_t, ResourceStack> const>>
    resource_stacks() const;

    std::uint32_t total_weight() const;

    std::uint32_t max_weight() const;

  private:
    std::unordered_map<std::uint32_t, ResourceStack> stacks;
    std::uint32_t total_weight_ = 0; // gram(s)
    std::uint32_t max_weight_;
};

} // namespace lc1::game
