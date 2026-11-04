#pragma once

#include "lc1/game/character/bag.hpp"
#include "lc1/game/resources/resource.hpp"

#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace lc1::game {

// One recipe line: how many of which definition. Recipes are built from process-lifetime
// definitions, so this is the one place that still borrows instead of naming a keyid.
struct RecipeStack {
    // Required non-null borrow; the immutable definition must outlive the recipe.
    Resource const *resource;
    std::uint32_t quantity;
};

struct Recipe {
  public:
    Recipe(std::vector<RecipeStack> inputs, std::vector<RecipeStack> outputs);

    // Definition borrows must outlive this recipe and any bag receiving its outputs.
    std::vector<RecipeStack> inputs;
    std::vector<RecipeStack> outputs;
    std::uint64_t input_weight{};
    std::uint64_t output_weight{};
};

namespace detail {
std::optional<Bag> prepare_recipe(Bag const &bag, Recipe const &recipe);
} // namespace detail

bool can_craft(Bag const &bag, Recipe const &recipe);

bool craft(Bag &bag, Recipe const &recipe);

} // namespace lc1::game
