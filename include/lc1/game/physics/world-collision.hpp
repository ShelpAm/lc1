#pragma once

#include "lc1/game/continent/continent.hpp"

namespace lc1 {

// Read-only collision view. Borrows a non-null Continent, which must remain alive
// at the same address for the lifetime of the view. Geometry is never copied.
class WorldCollision {
  public:
    explicit WorldCollision(Continent const &continent);
    WorldCollision(Continent &&) = delete;
    WorldCollision(Continent const &&) = delete;

    std::optional<float> ground_height(glm::vec2 position) const;

    // Highest walkable surface at XZ that is not above `feet.y`, and no farther than
    // `max_distance` below it. Terrain and walkable block tops participate. A nonzero
    // footprint radius keeps a body supported while any part of its footprint overlaps
    // a block top, which prevents falling through a plinth edge and into its side.
    std::optional<GroundSample> support_below(glm::vec3 feet, float max_distance,
                                              float footprint_radius = 0.0F) const;

    // Lowest solid underside reached by the upward sweep of a square footprint.
    // head is the current top of the character, return value is the ceiling's world Y.
    std::optional<float> ceiling_above(glm::vec3 head, float max_distance,
                                       float footprint_radius) const;

    // Validates the upright body against static obstacles at its current feet height.
    bool can_stand(glm::vec3 feet, float half_width, float height) const;

    // Sweeps an upright box, then slides along contact planes. The result preserves the
    // input feet height; vertical support is queried separately by the character.
    glm::vec3 move(glm::vec3 feet, glm::vec2 displacement, float half_width, float height) const;

  private:
    Continent const *continent_;
};

} // namespace lc1
