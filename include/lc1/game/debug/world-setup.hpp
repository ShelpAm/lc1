#pragma once

#include "lc1/game/world/game-world.hpp"

namespace lc1::demo {
std::vector<ContinentBlock> showcase_blocks();
WorldSetup world_setup(ContinentIdentity const &identity);
} // namespace lc1::demo
