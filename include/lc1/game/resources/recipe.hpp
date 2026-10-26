#pragma once

#include "lc1/game/character/bag.hpp"

#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace lc1::game {

struct Recipe {
  public:
    Recipe(std::vector<ResourceStack> inputs, std::vector<ResourceStack> outputs);

    // Definition borrows must outlive this recipe and any bag receiving its outputs.
    std::vector<ResourceStack> inputs;
    std::vector<ResourceStack> outputs;
    std::uint64_t input_weight{};
    std::uint64_t output_weight{};
};

namespace detail {
std::optional<Bag> prepare_recipe(Bag const &bag, Recipe const &recipe);
} // namespace detail

bool can_craft(Bag const &bag, Recipe const &recipe);

bool craft(Bag &bag, Recipe const &recipe);

} // namespace lc1::game
