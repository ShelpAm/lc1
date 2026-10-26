#include "lc1/vk/resources/gpu-font.hpp"

#include "lc1/vk/core/device.hpp"

#include <cstdint>
#include <utility>

namespace lc1 {

GpuFont::GpuFont(Device const &device, text::GlyphAtlas atlas)
    : atlas_{std::move(atlas)},
      texture_{device, vk::Extent2D{.width = atlas_.extent().x, .height = atlas_.extent().y},
               atlas_.pixels(), TextureColorSpace::Linear},
      sampler_info_{
          .magFilter = vk::Filter::eLinear,
          .minFilter = vk::Filter::eLinear,
          .mipmapMode = vk::SamplerMipmapMode::eNearest,
          .addressModeU = vk::SamplerAddressMode::eClampToEdge,
          .addressModeV = vk::SamplerAddressMode::eClampToEdge,
          .addressModeW = vk::SamplerAddressMode::eClampToEdge,
          .mipLodBias = 0.0F,
          .anisotropyEnable = vk::False,
          .maxAnisotropy = 1.0F,
          .compareEnable = vk::False,
          .compareOp = vk::CompareOp::eAlways,
          .minLod = 0.0F,
          .maxLod = 0.0F,
          .borderColor = vk::BorderColor::eIntOpaqueBlack,
          .unnormalizedCoordinates = vk::False,
      }
{
}

} // namespace lc1

namespace lc1 {

text::GlyphAtlas const &GpuFont::atlas() const
{
    return atlas_;
}

GpuTexture const &GpuFont::texture() const
{
    return texture_;
}

vk::SamplerCreateInfo const &GpuFont::sampler_info() const
{
    return sampler_info_;
}

} // namespace lc1
