#include "render-device.hpp"

#include "lc1/platform/window.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/presentation/surface.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/ray-query-shadows.hpp"
#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr vk::Extent2D extent{4, 4};
using Pixels = std::array<glm::uvec4, 16>;
using Surfaces = std::array<glm::vec4, 16>;

struct Case {
    bool spatial = false;
    bool isolated = false;
    bool normal_edge = false;
    bool depth_edge = false;
    bool hard_light = false;
    bool background = false;
    bool valid = true;
    bool moving = false;
    bool wrong_id = false;
    bool wrong_normal = false;
    bool wrong_depth = false;
    bool outside = false;
    bool uniform_shadow = false;
    bool reproject = false;
    std::uint32_t history_length = 7;
};

glm::uvec4 run_case(lc1::Device const &device, lc1::GraphicsShaders const &shaders, Case test)
{
    Pixels raw{}, history{};
    Surfaces surface{}, old_surface{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            int const index = y * 4 + x;
            raw[index] = {test.uniform_shadow ? 0U : ((x + y) % 2 ? 255U : 0U), 0U, 1U << 16,
                          test.moving ? 1U : 0U};
            history[index] = {test.reproject ? 192U : 128U, 0U, (test.wrong_id ? 2U : 1U) << 16,
                              test.history_length};
            // Surface normal (0,0,1), encoded with the shader's documented 12+12-bit layout.
            surface[index] = {2.0F * (x + 0.5F) / 4 - 1, 1 - 2.0F * (y + 0.5F) / 4, 0.5F,
                              8390656.0F};
            old_surface[index] = surface[index];
            if (test.wrong_normal)
                old_surface[index].w = 16777215.0F;
            if (test.wrong_depth)
                old_surface[index].z = 0.9F;
            if (test.reproject)
                old_surface[index].x -= 0.5F;
        }
    if (test.spatial) {
        for (int index = 0; index < 16; ++index) {
            raw[index] = {255U, 0U, 1U << 16, test.history_length};
            if (index != 5) {
                if (test.isolated)
                    raw[index].z = 2U << 16;
                if (test.normal_edge)
                    surface[index].w = 16777215.0F;
                if (test.depth_edge)
                    surface[index].z += 0.25F;
            }
        }
        raw[5].x = 0;
        if (test.background)
            raw[5].z = 0;
    }
    auto make_input = [&](vk::Format format) {
        return lc1::GpuImage{device,
                             format,
                             extent,
                             1,
                             vk::SampleCountFlagBits::e1,
                             vk::ImageUsageFlagBits::eTransferDst |
                                 vk::ImageUsageFlagBits::eSampled,
                             vk::ImageAspectFlagBits::eColor};
    };
    auto make_output = [&](vk::Format format) {
        return lc1::GpuImage{device,
                             format,
                             extent,
                             1,
                             vk::SampleCountFlagBits::e1,
                             vk::ImageUsageFlagBits::eColorAttachment |
                                 vk::ImageUsageFlagBits::eTransferSrc,
                             vk::ImageAspectFlagBits::eColor};
    };
    std::array inputs{
        make_input(vk::Format::eR32G32B32A32Uint), make_input(vk::Format::eR32G32B32A32Sfloat),
        make_input(vk::Format::eR32G32B32A32Uint), make_input(vk::Format::eR32G32B32A32Sfloat)};
    std::array outputs{make_output(vk::Format::eR32G32B32A32Uint),
                       make_output(vk::Format::eR32G32B32A32Sfloat)};
    std::vector<lc1::GpuBuffer> staging;
    for (int i = 0; i < 4; ++i)
        staging.emplace_back(device, sizeof(raw), vk::BufferUsageFlagBits2::eTransferSrc,
                             vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    staging[0].upload(std::as_bytes(std::span{raw}));
    staging[1].upload(std::as_bytes(std::span{surface}));
    staging[2].upload(std::as_bytes(std::span{history}));
    staging[3].upload(std::as_bytes(std::span{old_surface}));
    lc1::TemporalShadowData temporal{
        .frame_info = {test.valid ? 1U : 0U, 0U, 32U, 1U},
    };
    if (test.outside)
        temporal.previous_view_projection[3][0] = 4.0F;
    if (test.reproject)
        temporal.previous_view_projection[3][0] = 0.5F;
    lc1::GpuBuffer uniform{device, sizeof(temporal),
                           vk::BufferUsageFlagBits2::eUniformBuffer |
                               vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                           vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    uniform.upload(std::as_bytes(std::span{&temporal, 1}));
    lc1::LightsData lights{};
    lights.count = 1;
    lights.lights[0].type = test.hard_light ? 0 : 3;
    lights.lights[0].radius = 1.0F;
    lc1::GpuBuffer light_uniform{device, sizeof(lights),
                                 vk::BufferUsageFlagBits2::eUniformBuffer |
                                     vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                                 vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    light_uniform.upload(std::as_bytes(std::span{&lights, 1}));
    auto readback = device.allocator().createBuffer(
        {.size = sizeof(raw), .usage = vk::BufferUsageFlagBits::eTransferDst},
        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
         .usage = vma::MemoryUsage::eAuto});
    lc1::DescriptorHeap heap{device, 0, 4, 2};
    lc1::PassData pass;
    pass.temporal = heap.allocate_buffer(uniform);
    pass.lights = heap.allocate_buffer(light_uniform);
    pass.current_visibility = heap.allocate_image(inputs[0]);
    pass.resolved_visibility = pass.current_visibility;
    pass.current_surface = heap.allocate_image(inputs[1]);
    pass.history_visibility = heap.allocate_image(inputs[2]);
    pass.history_surface = heap.allocate_image(inputs[3]);
    lc1::one_time_submit(device, [&](lc1::CommandBuffer &wrapped) {
        auto const &commands = wrapped.raii();
        wrapped.bind_descriptor_heap(heap);
        wrapped.push_data(std::span{&pass, 1});
        vk::BufferImageCopy const region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1}};
        for (int i = 0; i < 4; ++i) {
            inputs[i].transition_layout(
                commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite);
            commands.copyBufferToImage(*staging[i].raii(), *inputs[i].raii(),
                                       vk::ImageLayout::eTransferDstOptimal, region);
            inputs[i].transition_layout(
                commands, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead);
        }
        std::array<vk::RenderingAttachmentInfo, 2> attachments;
        for (int i = 0; i < 2; ++i) {
            outputs[i].transition_layout(commands, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eColorAttachmentOptimal,
                                         vk::PipelineStageFlagBits2::eNone, {},
                                         vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                         vk::AccessFlagBits2::eColorAttachmentWrite);
            attachments[i] = {.imageView = *outputs[i].view(),
                              .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                              .loadOp = vk::AttachmentLoadOp::eDontCare,
                              .storeOp = vk::AttachmentStoreOp::eStore};
        }
        auto const active_attachments = std::span{attachments}.first(test.spatial ? 1 : 2);
        commands.beginRendering(vk::RenderingInfo{.renderArea = {.extent = extent}, .layerCount = 1}
                                    .setColorAttachments(active_attachments));
        shaders.bind_to_command_buffer(wrapped);
        lc1::set_graphics_state(commands, {.color_attachment_count = test.spatial ? 1U : 2U});
        commands.setViewportWithCount(
            vk::Viewport{.y = 4, .width = 4, .height = -4, .maxDepth = 1});
        commands.setScissorWithCount(vk::Rect2D{.extent = extent});
        commands.draw(3, 1, 0, 0);
        commands.endRendering();
        outputs[0].transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                     vk::ImageLayout::eTransferSrcOptimal,
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                                     vk::PipelineStageFlagBits2::eCopy,
                                     vk::AccessFlagBits2::eTransferRead);
        commands.copyImageToBuffer(*outputs[0].raii(), vk::ImageLayout::eTransferSrcOptimal,
                                   *readback, region);
        vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                         .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                         .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                         .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
    });
    Pixels result;
    readback.getAllocation().copyToMemory(0, result.data(), sizeof(result));
    return result[5];
}
} // namespace

int main()
{
    try {
        lc1::VulkanLoader loader;
        lc1::Window window{64, 64, "temporal shadow GPU tests"};
        glfwHideWindow(window.handle());
        auto extensions = window.required_instance_extensions();
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        lc1::Instance instance{loader, std::move(extensions), {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        lc1::Surface surface{instance, window};
        auto device = lc1::test::make_render_device(instance, *surface.raii());
        lc1::GraphicsShaders shaders{device, "shaders/temporal-shadows.spv", "temporalVert",
                                     "temporalFrag"};
        auto check = [&](char const *name, Case test, unsigned expected, unsigned count) {
            auto const result = run_case(device, shaders, test);
            if ((result.x & 255U) != expected || result.w != count)
                throw std::runtime_error(
                    std::format("{}: visibility {}, history {}; expected {}, {}", name,
                                result.x & 255U, result.w, expected, count));
            std::cout << name << " passed\n";
        };
        check("accumulation", {}, 112, 8);
        check("history reset", {.valid = false}, 0, 1);
        check("moving receiver", {.moving = true}, 0, 1);
        check("surface identity", {.wrong_id = true}, 0, 1);
        check("normal rejection", {.wrong_normal = true}, 0, 1);
        check("depth rejection", {.wrong_depth = true}, 0, 1);
        check("outside history", {.outside = true}, 0, 1);
        check("moving shadow clamp", {.uniform_shadow = true}, 0, 8);
        check("camera reprojection", {.reproject = true}, 168, 8);
        check("history limit", {.history_length = 32}, 124, 32);
        check("empty history", {.history_length = 0}, 0, 1);
        lc1::GraphicsShaders spatial_shaders{device, "shaders/spatial-shadows.spv", "spatialVert",
                                             "spatialFrag"};
        auto spatial_check = [&](char const *name, Case test, unsigned minimum, unsigned maximum) {
            auto const result = run_case(device, spatial_shaders, test);
            unsigned const value = result.x & 255U;
            if (value < minimum || value > maximum || result.w != test.history_length ||
                result.z != (test.background ? 0U : 1U << 16))
                throw std::runtime_error(
                    std::format("{}: unexpected spatial result {}", name, value));
            std::cout << name << " passed\n";
        };
        spatial_check("spatial noise reduction", {.spatial = true, .history_length = 1}, 120, 240);
        spatial_check("spatial identity edge", {.spatial = true, .isolated = true}, 0, 0);
        spatial_check("spatial normal edge", {.spatial = true, .normal_edge = true}, 0, 0);
        spatial_check("spatial depth edge", {.spatial = true, .depth_edge = true}, 0, 0);
        spatial_check("spatial hard light", {.spatial = true, .hard_light = true}, 0, 0);
        spatial_check("spatial background", {.spatial = true, .background = true}, 0, 0);
        spatial_check("spatial stable contrast", {.spatial = true, .history_length = 32}, 0, 0);
        device.wait_idle();
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
