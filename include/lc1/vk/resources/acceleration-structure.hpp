#pragma once

#include "lc1/vk/resources/gpu-buffer.hpp"

namespace lc1 {

// Owns storage and build scratch. Device and referenced BLASes must outlive it.
// Rebuild only after this frame slot's previous GPU use has completed.
class AccelerationStructure {
  public:
    AccelerationStructure(Device const &device, vk::AccelerationStructureTypeKHR type,
                          vk::AccelerationStructureGeometryKHR const &geometry,
                          std::uint32_t capacity);
    AccelerationStructure(AccelerationStructure const &) = delete;
    AccelerationStructure &operator=(AccelerationStructure const &) = delete;
    AccelerationStructure(AccelerationStructure &&) = default;
    // Memberwise assignment would free backing storage before the old AS handle.
    AccelerationStructure &operator=(AccelerationStructure &&) = delete;
    ~AccelerationStructure() = default;

    void build(vk::raii::CommandBuffer const &commands,
               vk::AccelerationStructureGeometryKHR const &geometry,
               std::uint32_t primitive_count) const;
    vk::raii::AccelerationStructureKHR const &raii() const;
    vk::DeviceAddress address() const;

  private:
    vk::AccelerationStructureTypeKHR type_;
    std::uint32_t capacity_;
    vk::AccelerationStructureBuildSizesInfoKHR sizes_;
    GpuBuffer storage_;
    GpuBuffer scratch_;
    vk::DeviceAddress scratch_address_;
    // Destroy the AS before its backing storage.
    vk::raii::AccelerationStructureKHR handle_;
    vk::DeviceAddress address_;
};

} // namespace lc1
