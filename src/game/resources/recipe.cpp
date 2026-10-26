#include "lc1/game/resources/recipe.hpp"

namespace lc1::game {

Recipe::Recipe(std::vector<ResourceStack> inputs, std::vector<ResourceStack> outputs)
    : inputs(std::move(inputs)), outputs(std::move(outputs))
{
    for (auto const &input : this->inputs) {
        assert(input.resource);
        input_weight += static_cast<std::uint64_t>(input.resource->weight) * input.quantity;
    }

    for (auto const &output : this->outputs) {
        assert(output.resource);
        output_weight += static_cast<std::uint64_t>(output.resource->weight) * output.quantity;
    }
}

} // namespace lc1::game

namespace lc1::game::detail {

std::optional<Bag> prepare_recipe(Bag const &bag, Recipe const &recipe)
{
    // Work on a candidate so even allocation failure cannot partially consume inputs.
    auto candidate = bag;
    for (auto const &[resource, quantity] : recipe.inputs) {
        assert(resource);
        if (!candidate.remove(*resource, quantity))
            return std::nullopt;
    }
    for (auto const &[resource, quantity] : recipe.outputs) {
        assert(resource);
        if (!candidate.add(*resource, quantity))
            return std::nullopt;
    }
    return candidate;
}

} // namespace lc1::game::detail

namespace lc1::game {

bool can_craft(Bag const &bag, Recipe const &recipe)
{
    return detail::prepare_recipe(bag, recipe).has_value();
}

bool craft(Bag &bag, Recipe const &recipe)
{
    auto candidate = detail::prepare_recipe(bag, recipe);
    if (!candidate)
        return false;
    bag.stacks.swap(candidate->stacks);
    std::swap(bag.total_weight_, candidate->total_weight_);
    return true;
}

} // namespace lc1::game
