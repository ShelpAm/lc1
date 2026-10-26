#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/memory.hpp"

#include <cstddef>
#include <span>

namespace lc1 {

class Device;

// A GPU buffer and its VMA allocation. Knows nothing about what it holds:
// vertex, index, uniform and staging buffers differ only in `usage` and
// `allocation_flags`.
class GpuBuffer {
  public:
    GpuBuffer(Device const &device, vk::DeviceSize size, vk::BufferUsageFlags2 usage,
              vma::AllocationCreateFlags allocation_flags = {}, vk::DeviceSize min_alignment = 1);

    // Requires a buffer created with allocation flag
    // vma::AllocationCreateFlagBits::eHostAccessSequentialWrite.
    void upload(std::span<std::byte const> data);

    vk::DeviceAddress device_address() const;

    // Requires eMapped at allocation time; VMA owns the persistent mapping.
    void *mapped_address() const;
    void flush(vk::DeviceSize offset, vk::DeviceSize size) const;

    vk::raii::Buffer const &raii() const;

    vk::DeviceSize size() const;

  private:
    // Owns both the VkBuffer and the VmaAllocation, and frees them together.
    vma::raii::Buffer buffer_{nullptr};
    vk::DeviceSize size_;
};

} // namespace lc1
