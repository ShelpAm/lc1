#pragma once

#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/resources/gpu-image.hpp"

#include <array>
#include <memory>
#include <optional>

namespace lc1 {
class Device;
class RenderGraph;
class CommandBuffer;
struct FrameResources;
struct FrameScene;

// Matches DdgiData in ddgi_common.slang; scalar pointers occupy 64 bits.
struct DdgiData {
    std::array<glm::vec4, 4> origin_spacing;
    std::array<glm::vec4, 4> previous_origin;
    glm::vec4 sky_radiance;
    std::array<glm::uvec4, 4>
        update;         // First local probe, count per cascade, sequence, valid history.
    glm::uvec4 inputs;  // Previous irradiance, distance, current rays, sampler.
    glm::uvec4 outputs; // Current irradiance, distance, lights, half-resolution irradiance.
    vk::DeviceAddress geometry;
    vk::DeviceAddress scene;
    glm::uvec4 resolve; // Surface, full width/height, visibility.
    std::array<glm::vec4, 64> directions;
    vk::DeviceAddress emitters; // uint2 geometry/triangle indices.
    glm::uvec2 emission_info;   // Sample count, rays per cascade.
    glm::uvec4 emitter_images;  // Prepared triangles, cell masks, masks per cell, reserved.
};

static_assert(sizeof(DdgiData) == 1328 && offsetof(DdgiData, geometry) == 240);

// Local, single-bounce diffuse GI, sharing the shadow TLAS. Device outlives this pass.
// Histories are shared by consecutive submissions on the renderer's single queue,
// not by frame slot; all frame resources and borrowed meshes survive GPU use.
class Ddgi {
  public:
    explicit Ddgi(Device const &device);
    void add_passes(RenderGraph &graph, FrameResources &resources, vk::Extent2D extent,
                    std::shared_ptr<FrameScene const> scene);

  private:
    void record(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent,
                FrameScene const &scene);

    Device const *device_;
    GraphicsShaders emitter_shaders_;
    GraphicsShaders candidate_shaders_;
    GraphicsShaders trace_shaders_;
    GraphicsShaders blend_shaders_;
    GraphicsShaders resolve_shaders_;
    GraphicsState trace_state_{};
    GraphicsState blend_state_{.color_attachment_count = 2};

    struct History {
        GpuImage irradiance;
        GpuImage distance;
    };

    std::array<std::optional<History>, 2> history_;
    std::uint32_t write_index_ = 0;
    std::uint32_t sequence_ = 0;
    bool valid_ = false;
    std::array<glm::vec4, 4> previous_origins_{};
};
} // namespace lc1
