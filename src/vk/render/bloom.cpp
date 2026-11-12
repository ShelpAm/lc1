#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/command-buffer.hpp"

#include <algorithm>

namespace lc1 {
void Renderer::record_bloom(CommandBuffer &commands, FrameResources &resources, vk::Extent2D extent)
{
    vk::Extent2D const reduced{std::max(1U, (extent.width + 1) / 2),
                               std::max(1U, (extent.height + 1) / 2)};
    for (std::size_t i = 0; i < resources.bloom_images.size(); ++i) {
        auto &image = resources.bloom_images[i];
        if (!image || image->extent() != reduced)
            image.emplace(*device_, hdr_format, reduced, 1, vk::SampleCountFlagBits::e1,
                          vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eSampled,
                          vk::ImageAspectFlagBits::eColor);
        resources.image_index(resources.bloom_indices[i], *image);
    }
    auto sampler = default_sampler_info_;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        vk::SamplerAddressMode::eClampToEdge;
    sampler.anisotropyEnable = false;
    auto const filter = resources.sampler_index(resources.bloom_sampler, sampler);
    resources.pass_data.bloom_image = *resources.bloom_indices[0];
    resources.pass_data.bloom_sampler = filter;
    commands.bind_descriptor_heap(*resources.descriptor_heap);

    struct PushData {
        std::uint32_t image;
        std::uint32_t sampler;
        std::uint32_t mode;
        std::uint32_t reserved = 0;
        glm::vec4 dimensions;
    };

    static_assert(sizeof(PushData) == 32);
    for (std::uint32_t pass = 0; pass < 3; ++pass) {
        auto &target = *resources.bloom_images[pass % 2];
        // Discard old content, retaining dependencies on both old reads and writes.
        target.transition_layout(
            commands.raii(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite);
        vk::RenderingAttachmentInfo const attachment{.imageView = *target.view(),
                                                     .imageLayout =
                                                         vk::ImageLayout::eColorAttachmentOptimal,
                                                     .loadOp = vk::AttachmentLoadOp::eDontCare,
                                                     .storeOp = vk::AttachmentStoreOp::eStore};
        vk::RenderingInfo rendering{.renderArea = {.offset = {0, 0}, .extent = reduced},
                                    .layerCount = 1};
        rendering.setColorAttachments(attachment);
        commands.raii().beginRendering(rendering);
        bloom_shaders_.bind_to_command_buffer(commands);
        set_graphics_state(commands.raii());
        commands.raii().setViewportWithCount(
            vk::Viewport{.x = 0,
                         .y = 0,
                         .width = static_cast<float>(reduced.width),
                         .height = static_cast<float>(reduced.height),
                         .minDepth = 0,
                         .maxDepth = 1});
        commands.raii().setScissorWithCount(vk::Rect2D{.offset = {0, 0}, .extent = reduced});
        auto const source_extent = pass == 0 ? extent : reduced;
        PushData const data{.image = pass == 0 ? resources.pass_data.hdr_image
                                               : *resources.bloom_indices[(pass - 1) % 2],
                            .sampler = filter,
                            .mode = pass,
                            .dimensions = {source_extent.width, source_extent.height, reduced.width,
                                           reduced.height}};
        commands.push_data(std::span{&data, 1});
        commands.raii().draw(3, 1, 0, 0);
        commands.raii().endRendering();
        target.transition_layout(commands.raii(), vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eShaderReadOnlyOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eFragmentShader,
                                 vk::AccessFlagBits2::eShaderSampledRead);
    }
}
} // namespace lc1
