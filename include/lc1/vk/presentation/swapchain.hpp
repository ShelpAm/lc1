#pragma once

#include "lc1/vk/core/common.hpp"

#include <cstdint>
#include <vector>

namespace lc1 {

class Device;
class Surface;

// Choices supplied by the caller, not selected by Swapchain. Capabilities
// must be queried again before recreation; the requested minimum can differ
// from the actual image count returned by Vulkan.
struct SwapchainConfig {
    vk::SurfaceFormatKHR surface_format;
    vk::PresentModeKHR present_mode;
    std::uint32_t min_image_count;
    vk::CompositeAlphaFlagBitsKHR composite_alpha;
};

// One swapchain image plus everything whose lifetime must match it.
//
// The present semaphore lives here rather than in the frame loop.
// vkGetSwapchainImagesKHR may return MORE images than requested, and the count
// can differ between the original and a recreated swapchain, so a separate
// semaphore array sized from minImageCount makes renderFinished[imageIndex] an
// out-of-bounds write. Tying the semaphore to the image makes "array sized
// wrong" and "recreation forgot a resource" the same bug, caught in one place.
struct SwapchainImage {
    // Non-owning: the swapchain owns this image and destroys it with itself.
    // vk::raii::Image would call vkDestroyImage on it, which is invalid for
    // swapchain images.
    vk::Image image{};
    vk::raii::ImageView view{nullptr};
    vk::raii::Semaphore render_finished{nullptr};
};

// Must not outlive the Device it was constructed from: its raii handles are
// destroyed through that device. The presentation Surface must also outlive it.
//
// Construction creates the swapchain: format, extent and images are valid
// immediately. The caller resolves a nonzero extent from the surface first.
class Swapchain {
  public:
    Swapchain(Device const &device, Surface const &surface, SwapchainConfig const &config,
              vk::Extent2D extent);

    // Rebuilds the swapchain after a resize, VK_ERROR_OUT_OF_DATE_KHR, or
    // VK_SUBOPTIMAL_KHR.
    // Validates the supplied choices before releasing existing resources.
    void recreate(SwapchainConfig const &config, vk::Extent2D extent);

    vk::Format format() const;

    vk::Extent2D extent() const;

    std::vector<SwapchainImage> const &images() const;

    // For FrameLoop: acquireNextImage is a SwapchainKHR method.
    vk::raii::SwapchainKHR const &raii() const;

  private:
    Device const *device_; // Non-owning, non-null; the device outlives this object.
    // Non-owning: Surface must outlive this swapchain, including recreation.
    Surface const *surface_;

    vk::raii::SwapchainKHR swapchain_;
    vk::Format format_;
    vk::Extent2D extent_;
    // Declared last so it is destroyed first: the image views and semaphores in
    // here reference the swapchain's images and must not outlive it on the way
    // out of scope. recreate() has to repeat that ordering by hand.
    std::vector<SwapchainImage> images_;
};

} // namespace lc1
