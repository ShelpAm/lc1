#pragma once

#include "lc1/vk/resources/gpu-image.hpp"

#include <cstdint>
#include <span>

namespace lc1 {

class Device;
struct Image;

enum class TextureColorSpace { Srgb, Linear };

// An uploaded image shared like GpuMesh: a hundred soldiers carrying the same
// banner sample one GpuTexture. After construction the image is in
// SHADER_READ_ONLY_OPTIMAL, ready for the fragment shader to sample.
class GpuTexture {
  public:
    // Upload completes before returning; the CPU pixels need not outlive this call.
    GpuTexture(Device const &device, Image const &pixels,
               TextureColorSpace color_space = TextureColorSpace::Srgb);
    // Tightly packed RGBA8; also used for procedural and fallback textures.
    GpuTexture(Device const &device, vk::Extent2D extent, std::span<std::uint8_t const> pixels,
               TextureColorSpace color_space = TextureColorSpace::Srgb);

    GpuTexture(GpuTexture const &) = delete;
    GpuTexture(GpuTexture &&) = default;
    GpuTexture &operator=(GpuTexture const &) = delete;
    GpuTexture &operator=(GpuTexture &&) = default;
    ~GpuTexture() = default;

    GpuImage const &image() const;
    TextureColorSpace color_space() const;

  private:
    GpuImage image_;
    TextureColorSpace color_space_;
};

} // namespace lc1
