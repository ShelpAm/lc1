#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

namespace lc1 {

class CommandBuffer;

using HeapIndex = std::uint32_t;

class SamplerHeap {
    friend class DescriptorHeap;

  public:
    SamplerHeap(Device const &device, vk::DeviceSize capacity,
                vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties);

    HeapIndex allocate_sampler();
    HeapIndex allocate_sampler(vk::SamplerCreateInfo const &info);
    // The caller must wait for every GPU use of this slot before overwriting it.
    void write_sampler(HeapIndex index, vk::SamplerCreateInfo const &info);

    void deallocate_sampler(HeapIndex index); // TODO: implement

  private:
    Device const *device_;
    vk::DeviceSize reserved_size_;
    vk::DeviceSize offset_;
    vk::DeviceSize stride_;
    std::size_t capacity_;
    GpuBuffer buffer_;
    std::size_t size_;
};

class ResourceHeap {
    friend class DescriptorHeap;

  public:
    ResourceHeap(Device const &device, vk::DeviceSize image_capacity,
                 vk::DeviceSize buffer_capacity,
                 vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties,
                 vk::DeviceSize acceleration_capacity = 0);

    ResourceHeap(ResourceHeap const &) = delete;
    ResourceHeap(ResourceHeap &&) = delete;
    ResourceHeap &operator=(ResourceHeap const &) = delete;
    ResourceHeap &operator=(ResourceHeap &&) = delete;
    ~ResourceHeap() = default;

    HeapIndex allocate_image(GpuImage const &image,
                             vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
    // The caller must wait for every GPU use of this slot before overwriting it.
    void write_image(HeapIndex index, GpuImage const &image,
                     vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
    HeapIndex allocate_buffer(GpuBuffer const &buffer);
    // Borrows a live address returned by getAccelerationStructureAddressKHR.
    HeapIndex allocate_acceleration_structure(vk::DeviceAddress address);

    void deallocate_image(HeapIndex index); // TODO: implement

    void deallocate_buffer(HeapIndex index); // TODO: implement

  private:
    Device const *device_;
    vk::DeviceSize reserved_size_;
    vk::DeviceSize image_offset_;
    vk::DeviceSize image_stride_;
    std::size_t image_capacity_;
    vk::DeviceSize buffer_offset_;
    vk::DeviceSize buffer_stride_;
    std::size_t buffer_capacity_;
    vk::DeviceSize acceleration_stride_;
    vk::DeviceSize acceleration_offset_;
    vk::DeviceSize acceleration_capacity_;
    std::size_t acceleration_size_ = 0;
    GpuBuffer buffer_;
    std::size_t image_size_;
    std::size_t buffer_size_;
};

// It's worth noting that:
// (from https://docs.vulkan.org/guide/latest/descriptor_heap.html#_allocating_the_heap)
// Make sure if writing to the heap on the GPU, prior to reading descriptors from the heap on the
// GPU, to properly synchronize with VK_ACCESS_2_RESOURCE_HEAP_READ_BIT_EXT or
// VK_ACCESS_2_SAMPLER_HEAP_READ_BIT_EXT.
class DescriptorHeap {
    friend class CommandBuffer;

  public:
    DescriptorHeap(Device const &device, vk::DeviceSize sampler_capacity,
                   vk::DeviceSize image_capacity, vk::DeviceSize buffer_capacity,
                   vk::DeviceSize acceleration_capacity = 0);

    // Logs physical-device support, descriptor sizes/limits and this heap's allocation state.
    // Physical-device support does not imply that a feature was enabled on the logical device.
    void probe() const;

    // Returns an index relative to the whole heap, in units of the descriptor
    // type's size, ready for ResourceDescriptorHeap / SamplerDescriptorHeap.
    // Referenced resources must outlive all GPU submissions reading these slots.
    // Slots are append-only until deallocation is implemented.
    HeapIndex allocate_sampler();
    HeapIndex allocate_sampler(vk::SamplerCreateInfo const &info);
    // The caller must wait for every GPU use of this slot before overwriting it.
    void write_sampler(HeapIndex index, vk::SamplerCreateInfo const &info);
    HeapIndex allocate_image(GpuImage const &image,
                             vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
    // The caller must wait for every GPU use of this slot before overwriting it.
    void write_image(HeapIndex index, GpuImage const &image,
                     vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);
    HeapIndex allocate_buffer(GpuBuffer const &buffer);
    // Borrows a live address returned by getAccelerationStructureAddressKHR.
    HeapIndex allocate_acceleration_structure(vk::DeviceAddress address);

  private:
    // CommandBuffer is the only binding entry point, keeping its cache in sync.
    void bind_to_command_buffer(CommandBuffer &command_buffer) const;

    Device const *device_;

    vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties_;

    SamplerHeap sampler_heap_;
    ResourceHeap resource_heap_;
};

} // namespace lc1
