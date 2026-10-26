#include "lc1/text/glyph-atlas.hpp"

#include "lc1/error.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <spdlog/spdlog.h>

namespace lc1::text {

std::u32string ascii_codepoints()
{
    std::u32string codepoints;
    for (char32_t code_point = U' '; code_point <= U'~'; ++code_point)
        codepoints.push_back(code_point);
    return codepoints;
}

namespace {

// The face must be deleted before the library that created it.
using LibraryHandle = std::unique_ptr<FT_LibraryRec_, decltype(&FT_Done_FreeType)>;
using FaceHandle = std::unique_ptr<FT_FaceRec, decltype(&FT_Done_Face)>;

void check(FT_Error error, std::string_view action, std::filesystem::path const &ttf)
{
    if (error == 0)
        return;
    auto const *message = FT_Error_String(error);
    fail("{} {}: {}", action, ttf.string(), message != nullptr ? message : "FreeType error");
}

// Blank texels around every glyph keep linear filtering from pulling a
// neighbour's coverage across the edge of a quad.
constexpr std::uint32_t padding = 1;

// Pair kerning is looked up for every ordered pair of rasterized glyphs, so that pass is
// quadratic. A few hundred glyphs are instant; a large CJK set would spend minutes on pairs
// that almost never kern, while the pairs that do matter are the Latin ones a shaper would
// handle later. Above this many glyphs the atlas therefore reports zero for every pair,
// exactly as a face without a kern table does.
constexpr std::size_t kerning_glyph_limit = 4096;

struct Rasterized {
    char32_t code_point = 0;
    Glyph glyph;
    std::vector<std::uint8_t> coverage; // size.x * size.y bytes, row-major
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

} // namespace

GlyphAtlas::GlyphAtlas(std::filesystem::path const &ttf, GlyphAtlasOptions options)
{
    if (!(options.em_size > 0.0F))
        fail("font em size must be positive, got {}", options.em_size);

    FT_Library library = nullptr;
    check(FT_Init_FreeType(&library), "cannot initialize FreeType for", ttf);
    LibraryHandle const library_handle{library, &FT_Done_FreeType};

    FT_Face face = nullptr;
    check(FT_New_Face(library, ttf.string().c_str(), 0, &face), "cannot open font", ttf);
    FaceHandle const face_handle{face, &FT_Done_Face};

    check(FT_Select_Charmap(face, FT_ENCODING_UNICODE), "font has no Unicode charmap:", ttf);
    check(FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(options.em_size))),
          "font cannot be set to the requested em size:", ttf);

    em_size_ = options.em_size;
    auto const &size_metrics = face->size->metrics;
    line_height_ = static_cast<float>(size_metrics.height) / 64.0F;
    ascent_ = static_cast<float>(size_metrics.ascender) / 64.0F;
    descent_ = -static_cast<float>(size_metrics.descender) / 64.0F;

    // Rasterize before packing: placing glyphs needs every bitmap's size. A repeated
    // codepoint would otherwise rasterize twice, spend atlas space twice, and add a row and
    // a column to the kerning pass below.
    auto requested = options.codepoints.empty() ? ascii_codepoints() : options.codepoints;
    std::sort(requested.begin(), requested.end());
    requested.erase(std::unique(requested.begin(), requested.end()), requested.end());
    std::vector<Rasterized> rasters;
    rasters.reserve(requested.size());
    std::uint64_t area = 0;
    std::uint32_t widest = 0;
    for (auto const code_point : requested) {
        if (FT_Get_Char_Index(face, code_point) == 0)
            continue; // The face has no glyph for this codepoint.
        check(FT_Load_Char(face, code_point, FT_LOAD_DEFAULT), "cannot load glyph from", ttf);
        check(FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL), "cannot render glyph from", ttf);
        auto const &bitmap = face->glyph->bitmap;
        if (bitmap.rows != 0 && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            fail("font {} rasterized a glyph in unsupported pixel mode {}", ttf.string(),
                 static_cast<int>(bitmap.pixel_mode));

        Rasterized raster;
        raster.code_point = code_point;
        raster.glyph.advance = static_cast<float>(face->glyph->advance.x) / 64.0F;
        raster.glyph.size =
            glm::vec2{static_cast<float>(bitmap.width), static_cast<float>(bitmap.rows)};
        raster.glyph.bearing = glm::vec2{static_cast<float>(face->glyph->bitmap_left),
                                         static_cast<float>(face->glyph->bitmap_top)};
        raster.coverage.resize(static_cast<std::size_t>(bitmap.width) * bitmap.rows);
        for (std::uint32_t row = 0; row < bitmap.rows; ++row) {
            auto const *source = bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
            std::copy_n(source, bitmap.width,
                        raster.coverage.begin() + static_cast<std::ptrdiff_t>(row) * bitmap.width);
        }
        auto const cell_width = bitmap.width + 2 * padding;
        auto const cell_height = bitmap.rows + 2 * padding;
        area += static_cast<std::uint64_t>(cell_width) * cell_height;
        widest = std::max(widest, cell_width);
        rasters.push_back(std::move(raster));
    }
    if (rasters.empty())
        fail("font {} has no glyph for any requested codepoint", ttf.string());

    // Shelf packing: glyphs fill a row until the next one would not fit, and the
    // atlas width aims for a roughly square bitmap. Deterministic for a face and
    // a codepoint set.
    std::uint32_t const width =
        std::max({64U, std::bit_ceil(static_cast<std::uint32_t>(std::sqrt(area))), widest});
    std::uint32_t cursor_x = 0;
    std::uint32_t cursor_y = 0;
    std::uint32_t shelf_height = 0;
    for (auto &raster : rasters) {
        auto const cell_width = static_cast<std::uint32_t>(raster.glyph.size.x) + 2 * padding;
        auto const cell_height = static_cast<std::uint32_t>(raster.glyph.size.y) + 2 * padding;
        if (cursor_x + cell_width > width) {
            cursor_x = 0;
            cursor_y += shelf_height;
            shelf_height = 0;
        }
        raster.x = cursor_x + padding;
        raster.y = cursor_y + padding;
        cursor_x += cell_width;
        shelf_height = std::max(shelf_height, cell_height);
    }
    std::uint32_t const height = std::bit_ceil(std::max(cursor_y + shelf_height, 1U));
    extent_ = {width, height};

    // White RGB with coverage in alpha: the fragment shader multiplies its own
    // colour by the sampled alpha, so the atlas itself stays untinted. Everything
    // outside a glyph's bitmap keeps zero coverage, which is what padding needs.
    pixels_.assign(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t texel = 0; texel < pixels_.size(); texel += 4) {
        pixels_[texel] = 0xFF;
        pixels_[texel + 1] = 0xFF;
        pixels_[texel + 2] = 0xFF;
    }
    for (auto &raster : rasters) {
        auto const glyph_width = static_cast<std::uint32_t>(raster.glyph.size.x);
        auto const glyph_height = static_cast<std::uint32_t>(raster.glyph.size.y);
        for (std::uint32_t row = 0; row < glyph_height; ++row) {
            auto *destination = pixels_.data() +
                                (static_cast<std::size_t>(raster.y + row) * width + raster.x) * 4 +
                                3;
            auto const *source =
                raster.coverage.data() + static_cast<std::size_t>(row) * glyph_width;
            for (std::uint32_t column = 0; column < glyph_width; ++column)
                destination[column * 4] = source[column];
        }
        raster.glyph.uv_min = glm::vec2{static_cast<float>(raster.x) / static_cast<float>(width),
                                        static_cast<float>(raster.y) / static_cast<float>(height)};
        raster.glyph.uv_max =
            glm::vec2{static_cast<float>(raster.x + glyph_width) / static_cast<float>(width),
                      static_cast<float>(raster.y + glyph_height) / static_cast<float>(height)};
        glyphs_.emplace(raster.code_point, raster.glyph);
    }

    // Kerning is a property of pairs, so it is looked up once for the whole
    // rasterized set rather than lazily during layout, which is const.
    if (FT_HAS_KERNING(face) && rasters.size() <= kerning_glyph_limit) {
        for (auto const &left : rasters) {
            for (auto const &right : rasters) {
                FT_Vector delta{};
                check(FT_Get_Kerning(face, FT_Get_Char_Index(face, left.code_point),
                                     FT_Get_Char_Index(face, right.code_point), FT_KERNING_DEFAULT,
                                     &delta),
                      "cannot read kerning from", ttf);
                if (delta.x != 0)
                    kerning_.emplace((static_cast<std::uint64_t>(left.code_point) << 32) |
                                         right.code_point,
                                     static_cast<float>(delta.x) / 64.0F);
            }
        }
    }
    else if (FT_HAS_KERNING(face)) {
        spdlog::debug("[GlyphAtlas] {}: {} glyphs exceed the {} glyph kerning limit; pair "
                      "kerning skipped",
                      ttf.string(), rasters.size(), kerning_glyph_limit);
    }

    spdlog::debug("[GlyphAtlas] {}: {} glyphs, {}x{} atlas, {:.0F}px em, {} kerning pairs",
                  ttf.string(), glyphs_.size(), extent_.x, extent_.y, em_size_, kerning_.size());
}

Glyph const *GlyphAtlas::glyph(char32_t code_point) const
{
    auto const found = glyphs_.find(code_point);
    return found != glyphs_.end() ? &found->second : nullptr;
}

float GlyphAtlas::kerning(char32_t left, char32_t right) const
{
    auto const found = kerning_.find((static_cast<std::uint64_t>(left) << 32) | right);
    return found != kerning_.end() ? found->second : 0.0F;
}

TextMetrics GlyphAtlas::measure(std::string_view text, float line_height) const
{
    return measure_text(*this, text, line_height);
}

} // namespace lc1::text

namespace lc1::text {

float GlyphAtlas::em_size() const
{
    return em_size_;
}

float GlyphAtlas::line_height() const
{
    return line_height_;
}

float GlyphAtlas::line_height_for_em(float em) const
{
    return em * line_height_ / em_size_;
}

float GlyphAtlas::ascent() const
{
    return ascent_;
}

float GlyphAtlas::descent() const
{
    return descent_;
}

glm::uvec2 GlyphAtlas::extent() const
{
    return extent_;
}

std::span<std::uint8_t const> GlyphAtlas::pixels() const
{
    return pixels_;
}

} // namespace lc1::text
