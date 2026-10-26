#include "lc1/vk/presentation/swapchain.hpp"

#include "lc1/vk/core/device.hpp"
#include "lc1/vk/presentation/surface.hpp"

#include <algorithm>
#include <limits>
#include <print>
#include <vector>

namespace lc1 {
namespace {

char const *present_mode_name(vk::PresentModeKHR mode)
{
    switch (mode) {
    case vk::PresentModeKHR::eImmediate:
        return "immediate";
    case vk::PresentModeKHR::eMailbox:
        return "mailbox";
    case vk::PresentModeKHR::eFifo:
        return "fifo";
    case vk::PresentModeKHR::eFifoRelaxed:
        return "fifo-relaxed";
    default:
        return "other";
    }
}

} // namespace

Swapchain::Swapchain(Device const &device, Surface const &surface, SwapchainConfig const &config,
                     vk::Extent2D extent)
    : device_{&device}, surface_{&surface}, swapchain_(nullptr), format_{vk::Format::eUndefined},
      extent_{extent}
{
    recreate(config, extent);
}

void Swapchain::recreate(SwapchainConfig const &config, vk::Extent2D extent)
{
    // Reject an unusable extent before destroying any existing resources.
    // A minimized window is handled by the caller, which waits for events.
    if (extent.width == 0 || extent.height == 0) {
        fail("cannot create a swapchain with a zero extent");
    }

    try {
        vk::SurfaceKHR const surface = *surface_->raii();

        auto const caps = device_->raii_physical().getSurfaceCapabilitiesKHR(surface);

        if (!(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment)) {
            fail("surface does not support VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT");
        }

        if (extent.width < caps.minImageExtent.width || extent.width > caps.maxImageExtent.width ||
            extent.height < caps.minImageExtent.height ||
            extent.height > caps.maxImageExtent.height ||
            (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max() &&
             extent != caps.currentExtent)) {
            fail("swapchain extent {}x{} is not supported by the surface", extent.width,
                 extent.height);
        }
        if (config.min_image_count < caps.minImageCount ||
            (caps.maxImageCount != 0 && config.min_image_count > caps.maxImageCount)) {
            fail("requested swapchain image count {} is outside the surface limits",
                 config.min_image_count);
        }
        if (!(caps.supportedCompositeAlpha & config.composite_alpha)) {
            fail("requested composite alpha mode is not supported by the surface");
        }

        auto const formats = device_->raii_physical().getSurfaceFormatsKHR(surface);
        bool const any_format = formats.size() == 1 &&
                                formats.front().format == vk::Format::eUndefined &&
                                formats.front().colorSpace == config.surface_format.colorSpace;
        if (config.surface_format.format == vk::Format::eUndefined ||
            (!any_format && std::ranges::find(formats, config.surface_format) == formats.end())) {
            fail("requested surface format/color space is not supported");
        }
        auto const modes = device_->raii_physical().getSurfacePresentModesKHR(surface);
        if (std::ranges::find(modes, config.present_mode) == modes.end()) {
            fail("requested present mode is not supported by the surface");
        }

        // Present is a queue operation and it consumes the swapchain, so
        // device-idle is the correct guard before destroying any of this.
        device_->raii().waitIdle();

        // Order matters and the member declaration order cannot save us here --
        // this is a rebuild, not a destruction. The image views and semaphores
        // reference the swapchain's images, so they go first.
        images_.clear();
        swapchain_.clear();

        // Two fields are deliberately left at their defaults.
        // pQueueFamilyIndices: EXCLUSIVE sharing means one queue family, so
        // there is nothing to list. oldSwapchain: passing the old handle makes
        // recreation faster but inverts the destroy ordering (the old swapchain
        // must outlive the new one), and this rebuilds from scratch anyway.
        vk::SwapchainCreateInfoKHR info;
        info.setSurface(surface)
            .setMinImageCount(config.min_image_count)
            .setImageFormat(config.surface_format.format)
            .setImageColorSpace(config.surface_format.colorSpace)
            .setImageExtent(extent)
            .setImageArrayLayers(1)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setPreTransform(caps.currentTransform)
            .setCompositeAlpha(config.composite_alpha)
            .setPresentMode(config.present_mode)
            // Clipped: Happens when windows are in front of others.
            .setClipped(vk::True);

        swapchain_ = device_->raii().createSwapchainKHR(info);
        format_ = config.surface_format.format;
        extent_ = extent;

        // Query the ACTUAL image count -- it may exceed what we asked for.
        std::vector<vk::Image> const raw_images = swapchain_.getImages();

        images_.resize(raw_images.size());
        for (size_t i = 0; i < raw_images.size(); ++i) {
            images_[i].image = raw_images[i];

            vk::ImageViewCreateInfo view_info;
            // subresourceRange's default is all zeros, and levelCount = 0 is
            // invalid -- it has to be spelled out.
            view_info.setImage(raw_images[i])
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format_)
                .setSubresourceRange(
                    vk::ImageSubresourceRange{.aspectMask = vk::ImageAspectFlagBits::eColor,
                                              .levelCount = 1,
                                              .layerCount = 1})
                // Defaults, don't remap
                .setComponents({.r = vk::ComponentSwizzle::eIdentity,
                                .g = vk::ComponentSwizzle::eIdentity,
                                .b = vk::ComponentSwizzle::eIdentity,
                                .a = vk::ComponentSwizzle::eIdentity});

            images_[i].view = device_->raii().createImageView(view_info);
            images_[i].render_finished = device_->raii().createSemaphore(vk::SemaphoreCreateInfo{});
        }

        std::println("[lc1] swapchain: {}x{}, {} images, {}", extent_.width, extent_.height,
                     raw_images.size(), present_mode_name(config.present_mode));
    }
    catch (vk::SystemError const &error) {
        fail("swapchain recreation failed: {}", error.what());
    }
}

} // namespace lc1

namespace lc1 {

vk::Format Swapchain::format() const
{
    return format_;
}

vk::Extent2D Swapchain::extent() const
{
    return extent_;
}

std::vector<SwapchainImage> const &Swapchain::images() const
{
    return images_;
}

vk::raii::SwapchainKHR const &Swapchain::raii() const
{
    return swapchain_;
}

} // namespace lc1
