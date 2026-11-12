#pragma once

#include "lc1/game/world/game-world.hpp"

namespace lc1::demo {
inline constexpr glm::vec3 present_showcase_offset{-120.0F, 0.0F, 0.0F};
std::vector<ContinentBlock> showcase_blocks();
WorldSetup world_setup(ContinentIdentity const &identity);
} // namespace lc1::demo
