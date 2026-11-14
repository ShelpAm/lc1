#include "lc1/text/text-layout.hpp"

#include "lc1/text/glyph-atlas.hpp"
#include "lc1/text/utf8.hpp"

#include "lc1/error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lc1::text {
namespace {

std::vector<std::u32string> split_lines(std::u32string const &code_points)
{
    std::vector<std::u32string> lines(1);
    for (auto const code_point : code_points) {
        if (code_point == U'\n')
            lines.emplace_back();
        else
            lines.back().push_back(code_point);
    }
    return lines;
}

// Pen position after the last glyph, in atlas pixels.
float line_width(GlyphAtlas const &atlas, std::u32string const &line)
{
    float pen = 0.0F;
    char32_t previous = 0;
    for (auto const code_point : line) {
        auto const *glyph = atlas.glyph(code_point);
        if (glyph == nullptr)
            continue;
        if (previous != 0)
            pen += atlas.kerning(previous, code_point);
        pen += glyph->advance;
        previous = code_point;
    }
    return pen;
}

// Baseline of the first line relative to the placement origin: the origin sits on
// the first line's ascent line, so every line falls below it.
float first_baseline(GlyphAtlas const &atlas)
{
    return -atlas.ascent();
}

// Caller units per atlas pixel, with the one shared guard for every layout entry
// point. A zero or non-finite target line height would otherwise collapse or flip
// glyphs through the axis-aligned plane basis.
float layout_scale(GlyphAtlas const &atlas, float line_height)
{
    if (!std::isfinite(line_height) || !(line_height > 0.0F))
        fail("text line height must be finite and positive, got {}", line_height);
    return line_height / atlas.line_height();
}

TextMetrics measure_text_atlas_pixels(GlyphAtlas const &atlas, std::string_view text)
{
    auto const lines = split_lines(decode_utf8(text));
    TextMetrics metrics{.lines = lines.size()};
    for (auto const &line : lines)
        metrics.width = std::max(metrics.width, line_width(atlas, line));
    metrics.height = static_cast<float>(lines.size()) * atlas.line_height();
    return metrics;
}

} // namespace

PlaneBasis plane_basis(glm::vec3 normal)
{
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
        fail("text normal must be finite and nonzero");
    // Rescale first to avoid overflow/underflow for finite non-unit directions.
    auto const largest = std::max({std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)});
    if (largest == 0.0F)
        fail("text normal must be finite and nonzero");
    auto const plane_normal = glm::normalize(normal / largest);
    auto const reference_up = std::abs(plane_normal.y) > 0.999F ? glm::vec3{0.0F, 0.0F, 1.0F}
                                                                : glm::vec3{0.0F, 1.0F, 0.0F};
    auto const right = glm::normalize(glm::cross(reference_up, plane_normal));
    auto const up = glm::cross(plane_normal, right);
    return {right, up, glm::cross(right, up)};
}

TextMetrics measure_text(GlyphAtlas const &atlas, std::string_view text, float line_height)
{
    auto const scale = layout_scale(atlas, line_height);
    auto metrics = measure_text_atlas_pixels(atlas, text);
    metrics.width *= scale;
    metrics.height *= scale;
    return metrics;
}

void append_text(std::vector<Vertex> &vertices, GlyphAtlas const &atlas, std::string_view text,
                 TextPlacement const &placement)
{
    auto const lines = split_lines(decode_utf8(text));
    auto const baseline_step = atlas.line_height();
    auto const scale = layout_scale(atlas, placement.line_height);
    // The quad's front must agree with the rest of the project: the same vertex
    // order as Geometry::quad, whose normal is cross(right, up).
    auto const basis = plane_basis(placement.normal);
    auto const &plane_right = basis.right;
    auto const &plane_up = basis.up;
    auto const &plane_normal = basis.normal;

    float baseline = first_baseline(atlas);
    for (auto const &line : lines) {
        float pen = 0.0F;
        if (placement.center_x)
            pen = -line_width(atlas, line) * 0.5F;
        char32_t previous = 0;
        for (auto const &code_point : line) {
            auto const *glyph = atlas.glyph(code_point);
            if (glyph == nullptr)
                continue;
            if (previous != 0)
                pen += atlas.kerning(previous, code_point);
            previous = code_point;

            auto const left = pen + glyph->bearing.x;
            auto const top = baseline + glyph->bearing.y;
            auto const place = [&](float x, float y) {
                return placement.origin + (plane_right * x + plane_up * y) * scale;
            };
            if (glyph->size.x > 0.0F && glyph->size.y > 0.0F) {
                auto const bottom_left = place(left, top - glyph->size.y);
                auto const right = place(left + glyph->size.x, top - glyph->size.y) - bottom_left;
                auto const up = place(left, top) - bottom_left;
                // Same corner order as the mesh quads: bottom-left, then u, then
                // u + v, then v.
                auto const corner = [&](glm::vec3 point, glm::vec2 uv) {
                    vertices.push_back({.position = point,
                                        .normal = plane_normal,
                                        .color = placement.color,
                                        .uv = uv});
                };
                corner(bottom_left, {glyph->uv_min.x, glyph->uv_max.y});
                corner(bottom_left + right, {glyph->uv_max.x, glyph->uv_max.y});
                corner(bottom_left + right + up, {glyph->uv_max.x, glyph->uv_min.y});
                corner(bottom_left, {glyph->uv_min.x, glyph->uv_max.y});
                corner(bottom_left + right + up, {glyph->uv_max.x, glyph->uv_min.y});
                corner(bottom_left + up, {glyph->uv_min.x, glyph->uv_min.y});
            }
            pen += glyph->advance;
        }
        baseline -= baseline_step;
    }
}

} // namespace lc1::text
