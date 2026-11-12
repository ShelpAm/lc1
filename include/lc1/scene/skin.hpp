#pragma once

#include "lc1/vertex.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace lc1::scene {
struct Skin {
    std::vector<std::size_t> joints;
    std::vector<glm::mat4> inverse_bind_matrices;
};

struct VertexInfluences {
    glm::uvec4 joints{};
    glm::vec4 weights{};
};

// Validated binding: node and joints refer to the associated ModelDefinition.
struct SkinnedMesh {
    std::size_t node;
    Skin skin;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<VertexInfluences> influences;
    // Optional raster-only index prefix; the complete index list still casts shadows.
    std::optional<std::uint32_t> view_index_count{};
};
} // namespace lc1::scene
