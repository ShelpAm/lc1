#include "lc1/vk/resources/acceleration-structure.hpp"
#include "lc1/vk/core/device.hpp"

namespace lc1 {
namespace {
vk::AccelerationStructureBuildGeometryInfoKHR
build_info(vk::AccelerationStructureTypeKHR type,
           vk::AccelerationStructureGeometryKHR const &geometry)
{
    vk::AccelerationStructureBuildGeometryInfoKHR info{
        .type = type,
        .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
        .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
    };
    info.setGeometries(geometry);
    return info;
}

vk::DeviceSize scratch_alignment(Device const &device)
{
    return device.raii_physical()
        .getProperties2<vk::PhysicalDeviceProperties2,
                        vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
        .get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
        .minAccelerationStructureScratchOffsetAlignment;
}

vk::AccelerationStructureBuildSizesInfoKHR
build_sizes(Device const &device, vk::AccelerationStructureTypeKHR type,
            vk::AccelerationStructureGeometryKHR const &geometry, std::uint32_t capacity)
{
    return device.raii().getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice, build_info(type, geometry), capacity);
}
} // namespace

AccelerationStructure::AccelerationStructure(Device const &device,
                                             vk::AccelerationStructureTypeKHR type,
                                             vk::AccelerationStructureGeometryKHR const &geometry,
                                             std::uint32_t capacity)
    : type_{type}, capacity_{capacity}, sizes_{build_sizes(device, type, geometry, capacity)},
      storage_{device, sizes_.accelerationStructureSize,
               vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR |
                   vk::BufferUsageFlagBits2::eShaderDeviceAddress},
      scratch_{device, sizes_.buildScratchSize + scratch_alignment(device),
               vk::BufferUsageFlagBits2::eStorageBuffer |
                   vk::BufferUsageFlagBits2::eShaderDeviceAddress},
      scratch_address_{(device.raii().getBufferAddress({.buffer = *scratch_.raii()}) +
                        scratch_alignment(device) - 1) &
                       ~(scratch_alignment(device) - 1)},
      handle_{device.raii().createAccelerationStructureKHR(
          {.buffer = *storage_.raii(), .size = sizes_.accelerationStructureSize, .type = type})},
      address_{
          device.raii().getAccelerationStructureAddressKHR({.accelerationStructure = *handle_})}
{
}

void AccelerationStructure::build(vk::raii::CommandBuffer const &commands,
                                  vk::AccelerationStructureGeometryKHR const &geometry,
                                  std::uint32_t primitive_count) const
{
    if (primitive_count > capacity_)
        fail("acceleration structure build exceeds capacity");
    auto info = build_info(type_, geometry);
    info.dstAccelerationStructure = *handle_;
    info.scratchData.deviceAddress = scratch_address_;
    vk::AccelerationStructureBuildRangeInfoKHR const range{.primitiveCount = primitive_count};
    commands.buildAccelerationStructuresKHR(info, &range);
    // BLAS -> TLAS build, and TLAS -> fragment ray query. Applies to later submits too.
    vk::MemoryBarrier2 const barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                        vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
    commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
}

} // namespace lc1

namespace lc1 {

vk::raii::AccelerationStructureKHR const &AccelerationStructure::raii() const
{
    return handle_;
}

vk::DeviceAddress AccelerationStructure::address() const
{
    return address_;
}

} // namespace lc1
