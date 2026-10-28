#pragma once

#include "lc1/vertex.hpp"

#include <cstdint>
#include <vector>

namespace lc1 {

// Generates MikkTSpace tangents for UV0, splitting vertices at tangent/handedness
// discontinuities. Requires finite triangles with usable normals and nondegenerate
// geometry/UVs; throws without modifying either vector on invalid input or failure.
// Generate in authored mesh coordinates; node transforms are applied when drawing.
// Meshes without normal maps can keep the default absent tangents.
void generate_tangents(std::vector<Vertex> &vertices, std::vector<std::uint32_t> &indices);

// True for a finite nondegenerate tangent frame. Does not require a unit-length
// basis: the shader orthogonalizes and normalizes after transforming/interpolating.
bool valid_tangent_frame(Vertex const &vertex);

} // namespace lc1
