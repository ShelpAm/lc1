#pragma once

#include "lc1/vk/core/common.hpp"

#include <cstdint>

namespace lc1 {

enum class VertexInput { Empty, Mesh, Position };

// Pass-owned fixed-function configuration, not the command buffer's current state.
struct GraphicsState {
    VertexInput vertex_input = VertexInput::Empty;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    std::uint32_t color_attachment_count = 1;
    bool depth_test = false;
    bool depth_write = true;
    bool depth_bias = false;
    // Straight-alpha blending (src alpha, one minus src alpha); off keeps the
    // opaque passes byte-identical.
    bool blend = false;
};

// Establishes the pass's fixed-function state, including after an ImGui pipeline
// bind. The caller sets viewport/scissor WITH counts and per-draw front face.
// Empty input is a full-screen pass; mesh/position inputs use back-face culling.
void set_graphics_state(vk::raii::CommandBuffer const &commands, GraphicsState state = {});

} // namespace lc1
