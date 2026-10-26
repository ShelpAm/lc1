#include "lc1/scene/mesh.hpp"

namespace lc1::scene {

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
    : vertices_(std::move(vertices)), indices_(std::move(indices))
{
}

} // namespace lc1::scene
