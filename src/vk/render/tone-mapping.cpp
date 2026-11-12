#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/command-buffer.hpp"

namespace lc1 {

void Renderer::tone_map(CommandBuffer &wrapped, FrameResources &resources,
                        RenderTarget const &target, OutputSettings output) const
{
    auto const &commands = wrapped.raii();
    wrapped.bind_descriptor_heap(*resources.descriptor_heap);
    wrapped.push_data(std::span{&resources.pass_data, 1});
    ToneMapData const data{{output.exposure, output.encoding == OutputEncoding::Srgb ? 1.0F : 0.0F,
                            output.bloom_strength, 0.0F}};
    resources.tone_map_buffer.upload(std::as_bytes(std::span{&data, 1}));
    vk::RenderingAttachmentInfo const attachment{
        .imageView = **target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    vk::RenderingInfo rendering{.renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
                                .layerCount = 1};
    rendering.setColorAttachments(attachment);
    commands.beginRendering(rendering);
    tone_map_shaders_.bind_to_command_buffer(wrapped);
    set_graphics_state(commands, tone_map_state_);
    commands.setViewportWithCount(vk::Viewport{.x = 0.0F,
                                               .y = 0.0F,
                                               .width = static_cast<float>(target.extent.width),
                                               .height = static_cast<float>(target.extent.height),
                                               .minDepth = 0.0F,
                                               .maxDepth = 1.0F});
    commands.setScissorWithCount(vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = target.extent});
    commands.setFrontFace(vk::FrontFace::eCounterClockwise);
    commands.draw(3, 1, 0, 0);
    commands.endRendering();
}

} // namespace lc1
