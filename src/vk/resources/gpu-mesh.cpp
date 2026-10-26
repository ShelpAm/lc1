#include "lc1/vk/resources/gpu-mesh.hpp"
#include <print>

namespace lc1 {

void GpuMesh::build_acceleration_structure(Device const &device)
{
    if (vertex_count_ == 0 || index_count_ == 0 || index_count_ % 3 != 0 ||
        (index_type_ != vk::IndexType::eUint16 && index_type_ != vk::IndexType::eUint32))
        fail("ray query meshes require nonempty indexed triangles with uint16/uint32 indices");
    vk::AccelerationStructureGeometryTrianglesDataKHR const triangles{
        .vertexFormat = vk::Format::eR32G32B32Sfloat,
        .vertexData = vk::DeviceOrHostAddressConstKHR{device.raii().getBufferAddress(
                                                          {.buffer = *vertices_.raii()}) +
                                                      offsetof(Vertex, position)},
        .vertexStride = sizeof(Vertex),
        .maxVertex = vertex_count_ - 1,
        .indexType = index_type_,
        .indexData = vk::DeviceOrHostAddressConstKHR{device.raii().getBufferAddress(
            {.buffer = *indices_.raii()})},
    };
    vk::AccelerationStructureGeometryKHR geometry{
        .geometryType = vk::GeometryTypeKHR::eTriangles,
        .flags = vk::GeometryFlagBitsKHR::eOpaque,
    };
    geometry.geometry.triangles = triangles;
    acceleration_structure_.emplace(device, vk::AccelerationStructureTypeKHR::eBottomLevel,
                                    geometry, index_count_ / 3);
    one_time_submit(device, [&](vk::raii::CommandBuffer const &commands) {
        acceleration_structure_->build(commands, geometry, index_count_ / 3);
    });
}

void GpuMesh::draw(vk::raii::CommandBuffer const &command_buffer) const
{
    command_buffer.bindVertexBuffers(0, *vertices_.raii(), vk::DeviceSize{0});
    command_buffer.bindIndexBuffer(*indices_.raii(), vk::DeviceSize{0}, index_type_);
    spdlog::debug("[GpuMesh] drawing {} vertices, {} indices", vertex_count_, index_count_);
    command_buffer.drawIndexed(index_count_, 1, 0, 0, 0);
}

} // namespace lc1

namespace lc1 {

bool GpuMesh::has_tangents() const
{
    return has_tangents_;
}

vk::DeviceAddress GpuMesh::acceleration_structure_address() const
{
    return acceleration_structure_->address();
}

} // namespace lc1
