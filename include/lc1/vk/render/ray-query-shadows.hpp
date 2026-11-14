#pragma once

#include "lc1/scene/scene.hpp"
#include "lc1/vk/render/draw-item.hpp"

#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/render-data.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace lc1 {

class Device;
class CommandBuffer;
class RenderGraph;
struct FrameResources;
struct FrameScene;

namespace scene {
class FpsCamera;
} // namespace scene

struct TemporalShadowData {
    glm::mat4 previous_view_projection{1.0F};
    glm::uvec4 frame_info{}; // valid, sequence index, history limit, light count
};

static_assert(sizeof(TemporalShadowData) == 80 && offsetof(TemporalShadowData, frame_info) == 64);

// Ray-query visibility, temporal accumulation and spatial filtering. The Device
// outlives this pass; callers keep per-slot resources alive until GPU completion.
class RayQueryShadows {
  public:
    RayQueryShadows(Device const &device, vk::Format depth_format);

    // Adds scene preparation, visibility, temporal resolve and spatial filtering.
    // Requires imported "frame.inputs" and produces "shadow.filtered_visibility".
    // Call after the slot fence. Scene inputs are owned by the callbacks. This object
    // and frame resources must outlive execution. Execute the
    // graph once for this frame, before recording another frame on this object.
    void add_passes(RenderGraph &graph, FrameResources &resources, vk::Extent2D extent,
                    std::shared_ptr<FrameScene const> scene);

  private:
    void prepare_frame(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent,
                       scene::FpsCamera const &camera, std::span<scene::Light const> lights,
                       std::span<DrawItem const> draws,
                       std::optional<scene::DistanceFog> const &fog);
    void finish_frame(scene::FpsCamera const &camera, std::span<scene::Light const> lights,
                      std::span<DrawItem const> draws);
    void ensure_attachments(FrameResources &resources, vk::Extent2D extent) const;
    void update_acceleration_structure(vk::raii::CommandBuffer const &commands,
                                       FrameResources &resources,
                                       std::span<DrawItem const> draws) const;
    void record_visibility(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent,
                           std::span<DrawItem const> draws) const;
    void prepare_history(vk::raii::CommandBuffer const &commands, FrameResources &resources,
                         vk::Extent2D extent);
    void resolve_history(CommandBuffer &commands, FrameResources &resources,
                         vk::Extent2D extent) const;
    void filter_history(CommandBuffer &commands, FrameResources &resources,
                        vk::Extent2D extent) const;

    Device const *device_; // Non-owning, non-null; the device outlives this object.
    vk::Format depth_format_;
    GraphicsState visibility_state_{
        .vertex_input = VertexInput::Mesh, .color_attachment_count = 2, .depth_test = true};
    GraphicsShaders visibility_shaders_;
    GraphicsState temporal_state_{.color_attachment_count = 2};
    GraphicsShaders temporal_shaders_;
    GraphicsState spatial_state_{};
    GraphicsShaders spatial_shaders_;

    struct HistoryFrame {
        GpuImage visibility;
        GpuImage surface;
    };

    // Consecutive submissions share history, independently of frame slots.
    // Replacing these images requires waiting for ALL outstanding GPU use.
    std::array<std::optional<HistoryFrame>, 2> history_;
    std::uint32_t history_write_index_ = 0;
    std::uint32_t sequence_ = 0;
    bool history_valid_ = false;
    glm::mat4 previous_view_projection_{1.0F};
    std::vector<DrawItem> previous_draws_; // Borrowed mesh identities, never dereferenced.
    std::array<scene::Light, max_num_lights> previous_lights_{};
    std::uint32_t previous_light_count_ = 0;
};

} // namespace lc1
