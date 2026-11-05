#pragma once

#include <compare>
#include <cstdint>

namespace lc1 {
// Stable character identity, independent of an object's address or a player's identity.
struct CharacterId {
    std::uint32_t value;
    auto operator<=>(CharacterId const &) const = default;
};
} // namespace lc1
