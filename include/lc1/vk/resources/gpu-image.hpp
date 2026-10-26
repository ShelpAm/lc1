#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/memory.hpp"
#include <glm/glm.hpp>

namespace lc1 {

class Device;

vk::Format find_supported_format(Device const &device, std::vector<vk::Format> const &candidates,
                                 vk::ImageTiling tiling, vk::FormatFeatureFlags features);

vk::Format find_depth_format(Device const &device);

// A GPU image, its VMA allocation, and a view of the whole image. Knows nothing
// about what it holds: a texture and a depth buffer differ only in format and
// usage (and, for depth, the view's aspect). Filling it is the owner's job --
// see GpuTexture.
class GpuImage {
  public:
    GpuImage(Device const &device, vk::Format format, vk::Extent2D extent, std::uint32_t mip_levels,
             vk::SampleCountFlagBits samples, vk::ImageUsageFlags usage,
             vk::ImageAspectFlags aspect);

    vk::raii::Image const &raii() const;

    vk::raii::ImageView const &view() const;

    vk::Extent2D extent() const;

    vk::Format format() const;

    vk::ImageViewCreateInfo view_create_info() const;

    void transition_layout(vk::raii::CommandBuffer const &command_buffer,
                           vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                           vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
                           vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) const;

    // Requires every level in TRANSFER_DST_OPTIMAL, with level 0 filled.
    // Checks linear blit support when there is more than one level, then
    // leaves every level in SHADER_READ_ONLY_OPTIMAL for fragment sampling.
    void generate_mipmaps(vk::raii::CommandBuffer const &command_buffer) const;

  private:
    // Owns both the VkImage and the VmaAllocation, and frees them together.
    // Must stay a vma::raii::Image: moving it into a vk::raii::Image keeps the
    // image but frees the memory at the end of that statement.
    vma::raii::Image image_{nullptr};
    // Declared after image_ so it is destroyed first: the view refers to it.
    vk::raii::ImageView view_{nullptr};
    vk::Format format_;
    vk::FormatFeatureFlags optimal_tiling_features_;
    vk::Extent2D extent_;
    std::uint32_t mip_levels_;
    vk::ImageAspectFlags aspect_;
};

} // namespace lc1
