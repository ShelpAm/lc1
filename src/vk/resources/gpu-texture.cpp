#include "lc1/vk/resources/gpu-texture.hpp"

#include "lc1/image.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace lc1 {
namespace {

constexpr std::uint32_t max_supported_mip_levels(vk::Extent2D extent)
{
    return static_cast<std::uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
}

GpuImage make_texture_image(Device const &device, vk::Extent2D extent,
                            std::span<std::uint8_t const> pixels, TextureColorSpace color_space)
{
    auto const limit = device.raii_physical().getProperties().limits.maxImageDimension2D;
    if (extent.width == 0 || extent.height == 0 || extent.width > limit || extent.height > limit ||
        pixels.size() != std::uint64_t{extent.width} * extent.height * 4)
        fail("texture requires valid dimensions and tightly packed RGBA8 pixels");
    auto const format = color_space == TextureColorSpace::Srgb ? vk::Format::eR8G8B8A8Srgb
                                                               : vk::Format::eR8G8B8A8Unorm;
    constexpr auto required = vk::FormatFeatureFlagBits::eSampledImage |
                              vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    if ((device.raii_physical().getFormatProperties(format).optimalTilingFeatures & required) !=
        required)
        fail("texture format {} does not support filtered sampling", vk::to_string(format));
    return {device,
            format,
            extent,
            max_supported_mip_levels(extent),
            vk::SampleCountFlagBits::e1,
            vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor};
}

} // namespace

GpuTexture::GpuTexture(Device const &device, Image const &pixels, TextureColorSpace color_space)
    : GpuTexture(device, {.width = pixels.extent.x, .height = pixels.extent.y}, pixels.bytes(),
                 color_space)
{
}

GpuTexture::GpuTexture(Device const &device, vk::Extent2D extent,
                       std::span<std::uint8_t const> pixels, TextureColorSpace color_space)
    : image_(make_texture_image(device, extent, pixels, color_space)), color_space_(color_space)
{
    // Only needed until the copy has finished; one_time_submit waits for that,
    // so it can die at the end of this constructor.
    GpuBuffer staging{device, pixels.size_bytes(), vk::BufferUsageFlagBits2::eTransferSrc,
                      vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    staging.upload(std::as_bytes(pixels));

    vk::Image const image = *image_.raii();
    one_time_submit(device, [&](vk::raii::CommandBuffer const &command_buffer) {
        // Nothing earlier to wait for: the image is new. All mip levels are
        // transitioned here: level 0 is written by COPY, the rest by BLIT.
        image_.transition_layout(
            command_buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);

        vk::BufferImageCopy2 region{
            .bufferOffset = 0,
            // 0 and 0: the pixels are tightly packed, row after row.
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageOffset = {.x = 0, .y = 0, .z = 0},
            .imageExtent = {.width = extent.width, .height = extent.height, .depth = 1},
        };
        vk::CopyBufferToImageInfo2 copy{
            .srcBuffer = *staging.raii(),
            .dstImage = image,
            .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        };
        copy.setRegions(region);
        command_buffer.copyBufferToImage2(copy);

        image_.generate_mipmaps(command_buffer);
        // generate_mipmaps also transitions every mip to shader-read layout and
        // makes transfer writes visible to subsequent fragment shader reads.
    });
}

} // namespace lc1

namespace lc1 {

GpuImage const &GpuTexture::image() const
{
    return image_;
}

TextureColorSpace GpuTexture::color_space() const
{
    return color_space_;
}

} // namespace lc1
