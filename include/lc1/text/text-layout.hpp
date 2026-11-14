#pragma once

#include "lc1/vertex.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace lc1::text {

class GlyphAtlas;

// Places text in an oriented plane. Screen text uses the default +Z normal.
struct TextPlacement {
    // Top of the first line, on its ascent line; later lines fall along local -up.
    // The left edge, or the horizontal centre when `center_x` is set.
    glm::vec3 origin{};
    // Target height, in caller units, of the first line's ascent-to-descent box.
    // Layout scales atlas pixels by `line_height / atlas.line_height()`.
    float line_height = 1.0F;
    glm::vec3 color{1.0F};
    // Centres the text along the plane's right axis about origin.
    bool center_x = false;
    // Finite, nonzero front direction. Up is projected +Y, falling back to +Z
    // when nearly parallel to +Y; the resulting basis preserves front winding.
    glm::vec3 normal{0.0F, 0.0F, 1.0F};
};

// Orthonormal axes of the plane facing `normal`: right first, then up. Up is world +Y
// projected onto the plane, falling back to +Z when the normal is nearly vertical.
struct PlaneBasis {
    glm::vec3 right{};
    glm::vec3 up{};
    // Unit front direction: cross(right, up), the winding the glyph quads use.
    glm::vec3 normal{};
};

// Rejects a zero or non-finite normal, exactly as append_text does.
PlaneBasis plane_basis(glm::vec3 normal);

// Extent of a laid-out run in caller units for the requested line height.
struct TextMetrics {
    float width = 0.0F;
    float height = 0.0F;
    std::size_t lines = 0;
};

// Measures a run in the units of `line_height` (pixels for screen text, world
// units for world text), using the same pen arithmetic as append_text, kerning
// included. Width is the widest line. Unknown codepoints are skipped, so they
// contribute nothing.
TextMetrics measure_text(GlyphAtlas const &atlas, std::string_view text, float line_height);

// Appends six vertices per visible glyph (triangle list, no index buffer) with
// the winding the rest of the project uses: the front faces placement.normal.
// Glyphs without a bitmap (spaces, codepoints outside the atlas) only move the
// pen. '\n' starts a new line.
void append_text(std::vector<Vertex> &vertices, GlyphAtlas const &atlas, std::string_view text,
                 TextPlacement const &placement);

} // namespace lc1::text
