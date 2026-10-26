#pragma once

#include <functional>
#include <glm/glm.hpp>

namespace lc1 {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal{0.0F, 1.0F, 0.0F}; // Object-space unit surface normal.
    glm::vec3 color{1.0F};
    glm::vec2 uv; // texture coordinate
    // Object-space tangent; w is bitangent handedness (+/-1). Zero means absent.
    glm::vec4 tangent{0.0F};

    friend constexpr bool operator==(Vertex const &lhs, Vertex const &rhs) = default;
};

} // namespace lc1

namespace std {
template <> struct hash<lc1::Vertex> {
    size_t operator()(lc1::Vertex const &vertex) const;
};
} // namespace std
