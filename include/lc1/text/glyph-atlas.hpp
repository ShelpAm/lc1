#pragma once

#include "lc1/text/text-layout.hpp"
#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace lc1::text {

// The baseline codepoint set: printable ASCII, U+0020..U+007E. The atlas holds one fixed
// set, and layout silently skips what it does not hold, so a caller that draws text from
// data (save records, for example) appends those codepoints here before constructing the
// atlas rather than expecting each one to be covered.
std::u32string ascii_codepoints();

// One rasterized glyph, measured in atlas pixels. `bearing` is the offset from
// the pen position to the bitmap's top-left corner with y pointing up (FreeType's
// convention), so the bitmap's top edge sits `bearing.y` above the baseline.
// Glyphs without a bitmap (spaces, for example) keep their advance and report a
// zero size; text layout advances the pen without emitting geometry for them.
struct Glyph {
    glm::vec2 uv_min{};
    glm::vec2 uv_max{};
    glm::vec2 size{};
    glm::vec2 bearing{};
    float advance = 0.0F;
};

struct GlyphAtlasOptions {
    // Em size the face is rasterized at, in atlas pixels. This only controls glyph
    // resolution; callers choose a target line height when laying text out.
    float em_size = 48.0F;
    // Codepoints to rasterize. Empty means `ascii_codepoints()`; repeats are removed.
    std::u32string codepoints;
};

// A face rasterized once into one RGBA8 atlas plus its metrics. Vulkan-free:
// `pixels()` is plain CPU data that GpuFont uploads. FreeType is used only inside
// the constructor, so layout code and its tests never touch it.
class GlyphAtlas {
  public:
    explicit GlyphAtlas(std::filesystem::path const &ttf, GlyphAtlasOptions options = {});

    GlyphAtlas(GlyphAtlas const &) = delete;
    GlyphAtlas &operator=(GlyphAtlas const &) = delete;
    GlyphAtlas(GlyphAtlas &&) = default;
    GlyphAtlas &operator=(GlyphAtlas &&) = default;
    ~GlyphAtlas() = default;

    // Null when the face has no glyph for the codepoint or the charmap did not
    // map it, so a codepoint outside the rasterized set is simply not drawn.
    Glyph const *glyph(char32_t code_point) const;

    // Pen adjustment between two adjacent codepoints in pixels; zero when the
    // face has no kerning for the pair.
    float kerning(char32_t left, char32_t right) const;

    // Em size used when rasterizing the atlas, in atlas pixels.
    float em_size() const;

    // Line-box height in atlas pixels. The target line height is a caller unit;
    // layout scales atlas pixels by `target_line_height / line_height()`.
    float line_height() const;

    // Target line height, in caller units, for a requested em size. Callers that
    // think in font sizes use this instead of touching atlas metrics directly.
    float line_height_for_em(float em) const;

    // Positive above and below the baseline respectively, in atlas pixels.
    float ascent() const;

    float descent() const;

    glm::uvec2 extent() const;

    // Tightly packed RGBA8: white RGB, coverage in alpha.
    std::span<std::uint8_t const> pixels() const;

    TextMetrics measure(std::string_view text, float line_height) const;

  private:
    std::unordered_map<char32_t, Glyph> glyphs_;
    // Packed (left codepoint << 32 | right codepoint) -> pen adjustment.
    std::unordered_map<std::uint64_t, float> kerning_;
    std::vector<std::uint8_t> pixels_;
    glm::uvec2 extent_{};
    float em_size_ = 0.0F;
    float line_height_ = 0.0F;
    float ascent_ = 0.0F;
    float descent_ = 0.0F;
};

} // namespace lc1::text
