#pragma once

#include "lc1/tangent-space.hpp"
#include "lc1/vertex.hpp"
#include "lc1/vk/core/common.hpp"
#include "lc1/vk/resources/acceleration-structure.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"
#include <algorithm>
#include <optional>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <span>

namespace lc1 {

class Device;

// Something drawable: vertex data on the GPU plus how many vertices to draw.
// A resource, loaded once and shared -- a hundred soldiers are a hundred
// DrawItems pointing at one GpuMesh.
class GpuMesh {
  public:
    // Vertex data lives in DEVICE_LOCAL memory, which the CPU usually cannot map.
    // So it goes through a staging buffer: the CPU writes a host-visible copy, and
    // the GPU copies that into the device-local one.
    template <std::unsigned_integral T>
    GpuMesh(Device const &device, std::span<Vertex const> vertices, std::span<T const> indices);

    template <std::ranges::contiguous_range Container>
        requires std::unsigned_integral<std::ranges::range_value_t<Container>>
    GpuMesh(Device const &device, std::span<Vertex const> vertices, Container const &indices)
        : GpuMesh(device, vertices, std::span(indices))
    {
    }

    // Binds the vertex buffer and draws. The pipeline must already be bound.
    void draw(vk::raii::CommandBuffer const &command_buffer) const;

    bool has_tangents() const;

    vk::DeviceAddress acceleration_structure_address() const;

  private:
    void build_acceleration_structure(Device const &device);

    GpuBuffer vertices_;
    std::uint32_t vertex_count_;
    GpuBuffer indices_;
    std::uint32_t index_count_;
    vk::IndexType index_type_;
    std::optional<AccelerationStructure> acceleration_structure_;
    bool has_tangents_ = false;
};

template <std::unsigned_integral T>
inline GpuMesh::GpuMesh(Device const &device, std::span<Vertex const> vertices,
                        std::span<T const> indices)
    : vertices_{device, vertices.size_bytes(),
                vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eTransferDst |
                    vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR},
      vertex_count_{static_cast<std::uint32_t>(vertices.size())},
      indices_{device, indices.size_bytes(),
               vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eTransferDst |
                   vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                   vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR},
      index_count_(static_cast<std::uint32_t>(indices.size())),
      index_type_(vk::IndexTypeValue<T>::value),
      has_tangents_(!vertices.empty() && std::ranges::all_of(vertices, valid_tangent_frame))
{
    // Only needed until the copy has finished; one_time_submit waits for that,
    // so it can die at the end of this constructor.
    GpuBuffer staging_vertices{device, vertices.size_bytes(),
                               vk::BufferUsageFlagBits2::eTransferSrc,
                               vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    staging_vertices.upload(std::as_bytes(vertices));

    GpuBuffer staging_indices{device, indices.size_bytes(), vk::BufferUsageFlagBits2::eTransferSrc,
                              vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    staging_indices.upload(std::as_bytes(indices));

    one_time_submit(device, [this, &staging_vertices, &vertices, &staging_indices,
                             &indices](vk::raii::CommandBuffer const &command_buffer) {
        command_buffer.copyBuffer(*staging_vertices.raii(), *vertices_.raii(),
                                  vk::BufferCopy{.size = vertices.size_bytes()});

        // The fence wait in one_time_submit only tells the CPU the copy is
        // done; it does not make the GPU's write visible to later GPU reads.
        // This barrier does: its second scope covers every later command on
        // the queue, including the vertex fetch in the frame loop's submits.
        vk::BufferMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput |
                            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask =
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eShaderRead,
            .buffer = *vertices_.raii(),
            .offset = 0,
            .size = vk::WholeSize,
        };
        vk::DependencyInfo dependency;
        dependency.setBufferMemoryBarriers(barrier);
        command_buffer.pipelineBarrier2(dependency);

        command_buffer.copyBuffer(*staging_indices.raii(), *indices_.raii(),
                                  vk::BufferCopy{.size = indices.size_bytes()});

        vk::BufferMemoryBarrier2 barrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eIndexInput |
                            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask = vk::AccessFlagBits2::eIndexRead | vk::AccessFlagBits2::eShaderRead,
            .buffer = *indices_.raii(),
            .offset = 0,
            .size = vk::WholeSize,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setBufferMemoryBarriers(barrier2));
    });

    build_acceleration_structure(device);

    spdlog::debug("[GpuMesh] loaded {} vertices, {} indices", vertex_count_, index_count_);
}

} // namespace lc1
