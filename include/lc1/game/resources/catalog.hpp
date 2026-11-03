#pragma once

#include "lc1/game/resources/resource.hpp"

#include <array>

namespace lc1::game {
// Immutable built-in definitions. Names and definitions have process lifetime.
// NOLINTBEGIN
// clang-format off
inline Resource const moneta{.keyid=1, .name="Moneta", .category=ResourceCategory::Currency, .weight=10};
inline Resource const wood{.keyid=2, .name="Wood", .category=ResourceCategory::Material, .weight=1000};
inline Resource const pig_iron{.keyid=3, .name="Pig iron", .category=ResourceCategory::Material, .weight=2000};
inline Resource const iron_helmet{.keyid=4, .name="Iron helmet", .category=ResourceCategory::Equipment, .weight=2500};
inline std::array const resources{moneta, wood, pig_iron, iron_helmet};
// clang-format on
// NOLINTEND
} // namespace lc1::game
