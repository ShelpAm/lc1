#pragma once

#include "lc1/error.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace lc1::scene {

// Model-local pose. Gameplay Transform keeps its existing degree-based Euler convention.
struct NodeTransform {
    glm::vec3 translation{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};

    glm::mat4 matrix() const;
};

struct ModelNode {
    std::string name;
    // Indices refer to the owning Model. Parents precede children; no parent means a root.
    std::optional<std::size_t> parent = std::nullopt;
    NodeTransform transform{};
    std::optional<std::size_t> mesh = std::nullopt;
};

} // namespace lc1::scene
