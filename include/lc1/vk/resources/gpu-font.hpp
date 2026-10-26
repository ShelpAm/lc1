#pragma once

#include "lc1/text/glyph-atlas.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <filesystem>

namespace lc1 {

class Device;

// A glyph atlas uploaded once and shared like GpuTexture: every label drawn with
// the same font samples one texture. The atlas is the CPU resource and this class
// only uploads it, the same division GpuTexture keeps with Image; layout works on
// the CPU atlas, so callers that only measure or place text never touch the GPU.
class GpuFont {
  public:
    GpuFont(Device const &device, text::GlyphAtlas atlas);

    GpuFont(GpuFont const &) = delete;
    GpuFont &operator=(GpuFont const &) = delete;
    GpuFont(GpuFont &&) = default;
    GpuFont &operator=(GpuFont &&) = default;
    ~GpuFont() = default;

    text::GlyphAtlas const &atlas() const;

    GpuTexture const &texture() const;

    // Clamped, with mip levels out of reach: the atlas cells only stay separate
    // while sampling stays at level 0, and one texel of padding separates them.
    vk::SamplerCreateInfo const &sampler_info() const;

  private:
    // The atlas owns the pixels the texture is uploaded from; declaration order
    // keeps it alive across the upload and afterwards for layout.
    text::GlyphAtlas atlas_;
    GpuTexture texture_;
    vk::SamplerCreateInfo sampler_info_;
};

} // namespace lc1
