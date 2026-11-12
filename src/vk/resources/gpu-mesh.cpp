#include "lc1/vk/resources/gpu-mesh.hpp"
#include <print>

namespace lc1 {

GpuMesh::GpuMesh(Device const &device, std::size_t vertex_count,
                 std::span<std::uint32_t const> indices)
    : vertices_{device, vertex_count * sizeof(Vertex),
                vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eTransferDst |
                    vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR},
      vertex_count_{static_cast<std::uint32_t>(vertex_count)},
      indices_{device, indices.size_bytes(),
               vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eTransferDst |
                   vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                   vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR},
      index_count_{static_cast<std::uint32_t>(indices.size())}, index_type_{vk::IndexType::eUint32}
{
    index_staging_.emplace(device, indices.size_bytes(), vk::BufferUsageFlagBits2::eTransferSrc,
                           vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    index_staging_->upload(std::as_bytes(indices));
    acceleration_structure_.emplace(device, vk::AccelerationStructureTypeKHR::eBottomLevel,
                                    acceleration_geometry(), index_count_ / 3);
}

vk::AccelerationStructureGeometryKHR GpuMesh::acceleration_geometry() const
{
    vk::AccelerationStructureGeometryTrianglesDataKHR const triangles{
        .vertexFormat = vk::Format::eR32G32B32Sfloat,
        .vertexData = vk::DeviceOrHostAddressConstKHR{vertices_.device_address() +
                                                      offsetof(Vertex, position)},
        .vertexStride = sizeof(Vertex),
        .maxVertex = vertex_count_ - 1,
        .indexType = index_type_,
        .indexData = vk::DeviceOrHostAddressConstKHR{indices_.device_address()},
    };
    vk::AccelerationStructureGeometryKHR geometry{
        .geometryType = vk::GeometryTypeKHR::eTriangles,
        .flags = vk::GeometryFlagBitsKHR::eOpaque,
    };
    geometry.geometry.triangles = triangles;
    return geometry;
}

void GpuMesh::build_acceleration_structure(Device const &device)
{
    if (vertex_count_ == 0 || index_count_ == 0 || index_count_ % 3 != 0 ||
        (index_type_ != vk::IndexType::eUint16 && index_type_ != vk::IndexType::eUint32))
        fail("ray query meshes require nonempty indexed triangles with uint16/uint32 indices");
    auto const geometry = acceleration_geometry();
    acceleration_structure_.emplace(device, vk::AccelerationStructureTypeKHR::eBottomLevel,
                                    geometry, index_count_ / 3);
    one_time_submit(device, [&](vk::raii::CommandBuffer const &commands) {
        acceleration_structure_->build(commands, geometry, index_count_ / 3);
    });
}

void GpuMesh::update_vertices(Device const &device, vk::raii::CommandBuffer const &commands,
                              std::span<Vertex const> vertices)
{
    // Order the previous use of this slot's vertex/index buffers, BLAS and scratch.
    vk::MemoryBarrier2 const before_update{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eCopy |
                        vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite |
                         vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                         vk::AccessFlagBits2::eAccelerationStructureWriteKHR};
    commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(before_update));
    if (vertices.size() != vertex_count_)
        fail("deformation cannot change mesh vertex count");
    if (!vertex_staging_)
        vertex_staging_.emplace(device, vertices.size_bytes(),
                                vk::BufferUsageFlagBits2::eTransferSrc,
                                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    has_tangents_ = std::ranges::all_of(vertices, valid_tangent_frame);
    vertex_staging_->upload(std::as_bytes(vertices));
    if (index_staging_) {
        commands.copyBuffer(*index_staging_->raii(), *indices_.raii(),
                            vk::BufferCopy{.size = index_count_ * sizeof(std::uint32_t)});
        vk::BufferMemoryBarrier2 const index_barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eIndexInput |
                            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                            vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eIndexRead | vk::AccessFlagBits2::eShaderRead,
            .buffer = *indices_.raii(),
            .offset = 0,
            .size = vk::WholeSize};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setBufferMemoryBarriers(index_barrier));
    }
    commands.copyBuffer(*vertex_staging_->raii(), *vertices_.raii(),
                        vk::BufferCopy{.size = vertices.size_bytes()});
    vk::BufferMemoryBarrier2 const barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput |
                        vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                        vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask =
            vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eShaderRead,
        .buffer = *vertices_.raii(),
        .offset = 0,
        .size = vk::WholeSize};
    commands.pipelineBarrier2(vk::DependencyInfo{}.setBufferMemoryBarriers(barrier));
    acceleration_structure_->build(commands, acceleration_geometry(), index_count_ / 3);
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

vk::DeviceAddress GpuMesh::vertex_address() const
{
    return vertices_.device_address();
}

vk::DeviceAddress GpuMesh::index_address() const
{
    return indices_.device_address();
}

std::uint32_t GpuMesh::index_stride() const
{
    return index_type_ == vk::IndexType::eUint16 ? 2U : 4U;
}

bool GpuMesh::has_tangents() const
{
    return has_tangents_;
}

vk::DeviceAddress GpuMesh::acceleration_structure_address() const
{
    return acceleration_structure_->address();
}

} // namespace lc1
