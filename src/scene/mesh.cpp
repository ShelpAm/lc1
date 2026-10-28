#include "lc1/scene/mesh.hpp"
#include "lc1/error.hpp"
#include "lc1/tangent-space.hpp"
#include <cmath>

namespace lc1::scene {

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
    : vertices_(std::move(vertices)), indices_(std::move(indices))
{
    if (indices_.size() % 3 != 0)
        fail("mesh indices must contain complete triangles");
    for (auto const index : indices_)
        if (index >= vertices_.size())
            fail("mesh index is outside the vertex array");
    auto finite = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    for (auto const &vertex : vertices_) {
        float const length = glm::length(vertex.normal);
        if (!finite(vertex.position) || !finite(vertex.normal) || !std::isfinite(length) ||
            length == 0.0F)
            fail("mesh vertex must have a finite position and nonzero finite normal");
        if (vertex.tangent.w != 0.0F && !valid_tangent_frame(vertex))
            fail("mesh requires a valid authored tangent frame");
    }
}

} // namespace lc1::scene
