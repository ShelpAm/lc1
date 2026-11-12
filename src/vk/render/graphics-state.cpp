#include "lc1/vk/render/graphics-state.hpp"

#include "vertex-input.hpp"

#include <array>
#include <span>

namespace lc1 {

void set_graphics_state(vk::raii::CommandBuffer const &commands, GraphicsState state)
{
    commands.setRasterizerDiscardEnable(vk::False);
    commands.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
    commands.setPrimitiveRestartEnable(vk::False);
    if (state.vertex_input == VertexInput::Empty) {
        commands.setVertexInputEXT({}, {});
    }
    else {
        constexpr auto binding = detail::vertex_binding_description();
        constexpr auto attributes = detail::vertex_attribute_descriptions();
        commands.setVertexInputEXT(
            binding, std::span{attributes}.first(
                         state.vertex_input == VertexInput::Position ? 1 : attributes.size()));
    }
    commands.setPolygonModeEXT(vk::PolygonMode::eFill);
    commands.setCullMode(state.vertex_input == VertexInput::Empty ? vk::CullModeFlagBits::eNone
                                                                  : vk::CullModeFlagBits::eBack);
    commands.setFrontFace(vk::FrontFace::eCounterClockwise);
    commands.setDepthClampEnableEXT(vk::False);
    commands.setDepthBiasEnable(state.depth_bias);
    commands.setDepthBias(state.depth_bias ? 1.25F : 0.0F, 0.0F, state.depth_bias ? 1.75F : 0.0F);
    commands.setLineWidth(1.0F);
    commands.setDepthTestEnable(state.depth_test);
    commands.setDepthWriteEnable(state.depth_test && state.depth_write);
    commands.setDepthCompareOp(vk::CompareOp::eLess);
    commands.setDepthBoundsTestEnable(vk::False);
    commands.setStencilTestEnable(vk::False);
    commands.setRasterizationSamplesEXT(state.samples);
    constexpr std::array<vk::SampleMask, 2> sample_mask{~0U, ~0U};
    commands.setSampleMaskEXT(
        state.samples,
        std::span{sample_mask}.first(state.samples == vk::SampleCountFlagBits::e64 ? 2 : 1));
    commands.setAlphaToCoverageEnableEXT(vk::False);
    commands.setAlphaToOneEnableEXT(vk::False);
    commands.setLogicOpEnableEXT(vk::False);
    for (std::uint32_t attachment = 0; attachment < state.color_attachment_count; ++attachment) {
        commands.setColorBlendEnableEXT(attachment, state.blend ? vk::True : vk::False);
        if (state.blend) {
            commands.setColorBlendEquationEXT(
                attachment, vk::ColorBlendEquationEXT{
                                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                                .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                                .colorBlendOp = vk::BlendOp::eAdd,
                                .srcAlphaBlendFactor = vk::BlendFactor::eOne,
                                .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                                .alphaBlendOp = vk::BlendOp::eAdd,
                            });
        }
        commands.setColorWriteMaskEXT(
            attachment, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    }
}

} // namespace lc1
