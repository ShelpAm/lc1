#pragma once

#include "lc1/game/character/character-id.hpp"
#include <functional>

namespace lc1 {
class Character;
// Simulation-local resolution. The referenced world must outlive any stored lookup.
using CharacterLookup = std::function<Character &(CharacterId)>;
} // namespace lc1
