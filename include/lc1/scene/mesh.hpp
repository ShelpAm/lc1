#pragma once

#include "lc1/vertex.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace lc1::scene {

class Mesh {
    friend class Model;

  public:
    Mesh(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);

  private:
    Mesh() = default;

    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
    // Matetial material_;
};

} // namespace lc1::scene
