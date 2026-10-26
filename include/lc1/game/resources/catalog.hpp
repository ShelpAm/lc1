#pragma once

#include "lc1/game/resources/resource.hpp"

#include <array>

namespace lc1::game {
// Immutable built-in definitions. Names and definitions have process lifetime.
inline Resource const moneta{1, "Moneta", ResourceCategory::Currency, 10};
inline Resource const wood{2, "Wood", ResourceCategory::Material, 1000};
inline Resource const pig_iron{3, "Pig iron", ResourceCategory::Material, 2000};
inline Resource const iron_helmet{4, "Iron helmet", ResourceCategory::Equipment, 2500};
inline std::array const resources{moneta, wood, pig_iron, iron_helmet};
} // namespace lc1::game
