#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/memory.hpp"
#include "lc1/vk/core/physical-device.hpp"

#include <cstdint>

namespace lc1 {

class Instance;

// Selected GPU, logical device, allocator and queue.
// Instance must outlive this object. The caller selects the physical device and
// queue family and verifies support for the requested features and extensions.
class Device {
  public:
    Device(Instance const &instance, vk::raii::PhysicalDevice physical, std::uint32_t queue_family,
           DeviceRequirements const &requirements);

    // Must be called explicitly before tearing down anything the GPU may still
    // be using: ~vk::raii::Device does not wait.
    void wait_idle() const;

    std::uint32_t queue_family() const;

    // Non-owning; valid only while this Device is.
    vk::raii::PhysicalDevice const &raii_physical() const;

    vk::raii::Device const &raii() const;

    MemoryAllocator const &allocator() const;

    vk::raii::Queue const &raii_queue() const;

    vk::SampleCountFlagBits max_sample_count() const;

  private:
    vk::raii::PhysicalDevice physical_;
    std::uint32_t queue_family_;
    vk::raii::Device device_;
    MemoryAllocator alloc_;
    vk::raii::Queue queue_;
};

} // namespace lc1
