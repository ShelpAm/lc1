#include "lc1/game/character/bag.hpp"

namespace lc1::game {

Bag::Bag(std::uint32_t max_weight) : max_weight_(max_weight)
{
}

bool Bag::add(Resource const &resource, std::uint32_t quantity)
{
    auto const added_weight = static_cast<std::uint64_t>(resource.weight) * quantity;
    if (added_weight > max_weight_ - total_weight_ ||
        quantity > std::numeric_limits<std::uint32_t>::max() - quantity_of(resource.keyid))
        return false;

    if (quantity == 0)
        return true;

    auto [result, inserted] =
        stacks.try_emplace(resource.keyid, ResourceStack{.resource = &resource, .quantity = 0});
    result->second.quantity += quantity;
    total_weight_ += static_cast<std::uint32_t>(added_weight);
    return true;
}

bool Bag::remove(Resource const &resource, std::uint32_t quantity)
{
    if (quantity == 0)
        return true;

    auto const found = stacks.find(resource.keyid);
    if (found == stacks.end() || found->second.quantity < quantity)
        return false;

    found->second.quantity -= quantity;
    total_weight_ -= static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(found->second.resource->weight) * quantity);
    if (found->second.quantity == 0)
        stacks.erase(found);
    return true;
}

std::uint32_t Bag::quantity_of(std::uint32_t keyid) const
{
    auto const found = stacks.find(keyid);
    return found == stacks.end() ? 0 : found->second.quantity;
}

std::ranges::values_view<
    std::ranges::ref_view<std::unordered_map<std::uint32_t, ResourceStack> const>>
Bag::resource_stacks() const
{
    return stacks | std::views::values;
}

std::uint32_t Bag::total_weight() const
{
    return total_weight_;
}

std::uint32_t Bag::max_weight() const
{
    return max_weight_;
}

} // namespace lc1::game
