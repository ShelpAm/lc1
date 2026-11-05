#pragma once

#include <compare>
#include <cstdint>

namespace lc1 {
// Player identity is independent of the controlled character's keyid.
struct PlayerId {
    std::uint32_t value;
    auto operator<=>(PlayerId const &) const = default;
};
} // namespace lc1
