#pragma once

namespace lc1 {

struct CharacterProportions {
    float scale = 1;
    float leg_height_scale = 1;

    bool operator==(CharacterProportions const &) const = default;
};

} // namespace lc1
