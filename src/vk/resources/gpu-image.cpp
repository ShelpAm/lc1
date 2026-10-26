#include "lc1/vk/resources/gpu-image.hpp"

#include "lc1/vk/core/device.hpp"

#include <spdlog/spdlog.h>

vk::Format lc1::find_supported_format(Device const &device,
                                      std::vector<vk::Format> const &candidates,
                                      vk::ImageTiling tiling, vk::FormatFeatureFlags features)
{
    for (auto const format : candidates) {
        vk::FormatProperties props = device.raii_physical().getFormatProperties(format);

        if (((tiling == vk::ImageTiling::eLinear) &&
             ((props.linearTilingFeatures & features) == features)) ||
            ((tiling == vk::ImageTiling::eOptimal) &&
             ((props.optimalTilingFeatures & features) == features))) {
            return format;
        }
    }

    fail("failed to find supported format");
}

namespace lc1 {

vk::Format find_depth_format(Device const &device)
{
    return find_supported_format(
        device, {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

GpuImage::GpuImage(Device const &device, vk::Format format, vk::Extent2D extent,
                   std::uint32_t mip_levels, vk::SampleCountFlagBits samples,
                   vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect)
    : image_{device.allocator().createImage(
          {
              .imageType = vk::ImageType::e2D,
              .format = format,
              .extent = vk::Extent3D{.width = extent.width, .height = extent.height, .depth = 1},
              .mipLevels = mip_levels,
              .arrayLayers = 1,
              .samples = samples,
              .tiling = vk::ImageTiling::eOptimal,
              .usage = usage,
              .sharingMode = vk::SharingMode::eExclusive,
              .initialLayout = vk::ImageLayout::eUndefined,
          },
          {
              // No host-access flag: images are filled through a staging
              // buffer, never mapped, so VMA picks DEVICE_LOCAL memory.
              .usage = vma::MemoryUsage::eAuto,
          })},
      view_{device.raii().createImageView({
          .image = *image_,
          .viewType = vk::ImageViewType::e2D,
          .format = format,
          .subresourceRange =
              {
                  .aspectMask = aspect,
                  .baseMipLevel = 0,
                  .levelCount = mip_levels,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
      })},
      format_{format},
      optimal_tiling_features_{
          device.raii_physical().getFormatProperties(format).optimalTilingFeatures},
      extent_{extent}, mip_levels_{mip_levels}, aspect_{aspect}
{
    spdlog::debug("[GpuImage] created a {}x{} image, format {}, usage {}", extent.width,
                  extent.height, vk::to_string(format), vk::to_string(usage));
}

vk::ImageViewCreateInfo GpuImage::view_create_info() const
{
    return vk::ImageViewCreateInfo{
        .image = *image_,
        .viewType = vk::ImageViewType::e2D,
        .format = format_,
        .subresourceRange =
            {
                .aspectMask = aspect_,
                .baseMipLevel = 0,
                .levelCount = mip_levels_,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
}

void GpuImage::transition_layout(vk::raii::CommandBuffer const &command_buffer,
                                 vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                 vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                                 vk::PipelineStageFlags2 dst_stage,
                                 vk::AccessFlags2 dst_access) const
{
    // subresourceRange has to be spelled out: unlike most hpp structs, whose
    // defaults are the Vulkan defaults, ImageSubresourceRange defaults to all
    // zeros -- and levelCount = 0 is invalid.
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *image_,
        .subresourceRange =
            vk::ImageSubresourceRange{
                .aspectMask = aspect_,
                .baseMipLevel = 0,
                .levelCount = mip_levels_,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vk::DependencyInfo dependency;
    dependency.setImageMemoryBarriers(barrier);

    command_buffer.pipelineBarrier2(dependency);
}

void GpuImage::generate_mipmaps(vk::raii::CommandBuffer const &command_buffer) const
{
    if (mip_levels_ > 1) {
        constexpr auto required = vk::FormatFeatureFlagBits::eBlitSrc |
                                  vk::FormatFeatureFlagBits::eBlitDst |
                                  vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        if ((optimal_tiling_features_ & required) != required) {
            fail("format {} does not support linear mipmap blitting", vk::to_string(format_));
        }
    }

    vk::ImageMemoryBarrier2 barrier{
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image_,
        .subresourceRange =
            vk::ImageSubresourceRange{
                .aspectMask = aspect_,
                .levelCount = 1,
                .layerCount = 1,
            },
    };

    int mip_width = static_cast<int>(extent_.width);
    int mip_height = static_cast<int>(extent_.height);
    // Every pass, copies level i to i+1.
    for (auto i = 0U; i != mip_levels_ - 1; ++i) {
        barrier.subresourceRange.baseMipLevel = i;

        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));

        vk::ImageBlit2 blit{
            .srcSubresource =
                {
                    .aspectMask = aspect_,
                    .mipLevel = i,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .srcOffsets = {{
                vk::Offset3D{.x = 0, .y = 0, .z = 0},
                vk::Offset3D{.x = mip_width, .y = mip_height, .z = 1},
            }},
            .dstSubresource =
                {
                    .aspectMask = aspect_,
                    .mipLevel = i + 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .dstOffsets = {{
                vk::Offset3D{.x = 0, .y = 0, .z = 0},
                vk::Offset3D{
                    .x = std::max(mip_width / 2, 1), .y = std::max(mip_height / 2, 1), .z = 1},
            }},
        };
        command_buffer.blitImage2({.srcImage = *image_,
                                   .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
                                   .dstImage = *image_,
                                   .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
                                   .regionCount = 1,
                                   .pRegions = &blit,
                                   .filter = vk::Filter::eLinear});

        mip_width = std::max(mip_width / 2, 1);
        mip_height = std::max(mip_height / 2, 1);

        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
    }

    barrier.subresourceRange.baseMipLevel = mip_levels_ - 1;
    // The last level was written, but never used as a blit source.
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
}

} // namespace lc1

namespace lc1 {

vk::raii::Image const &GpuImage::raii() const
{
    return image_;
}

vk::raii::ImageView const &GpuImage::view() const
{
    return view_;
}

vk::Extent2D GpuImage::extent() const
{
    return extent_;
}

vk::Format GpuImage::format() const
{
    return format_;
}

} // namespace lc1
