#pragma once

#include <cstdint>

namespace lc1::game {

// A reference to an item definition and how many of it. Entries carry the keyid instead of a
// borrowed definition, so a stack can be stored, copied and serialized; the definition is
// resolved wherever its name or weight is needed.
struct ResourceStack {
    std::uint32_t keyid;
    std::uint32_t quantity;
};

} // namespace lc1::game
