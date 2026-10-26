#pragma once

#include "lc1/game/resources/resource.hpp"

namespace lc1::game {

struct ResourceStack {
    // Required non-null borrow; the immutable definition must outlive this stack.
    Resource const *resource;
    std::uint32_t quantity;
};

} // namespace lc1::game
