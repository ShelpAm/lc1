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

    quantities_[resource.keyid] += quantity;
    total_weight_ += static_cast<std::uint32_t>(added_weight);
    return true;
}

bool Bag::remove(Resource const &resource, std::uint32_t quantity)
{
    if (quantity == 0)
        return true;

    auto const found = quantities_.find(resource.keyid);
    if (found == quantities_.end() || found->second < quantity)
        return false;

    found->second -= quantity;
    total_weight_ -=
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(resource.weight) * quantity);
    if (found->second == 0)
        quantities_.erase(found);
    return true;
}

std::uint32_t Bag::quantity_of(std::uint32_t keyid) const
{
    auto const found = quantities_.find(keyid);
    return found == quantities_.end() ? 0 : found->second;
}

std::unordered_map<std::uint32_t, std::uint32_t> const &Bag::quantities() const
{
    return quantities_;
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
