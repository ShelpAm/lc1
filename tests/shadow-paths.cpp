#include "render-device.hpp"

#include "lc1/image.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/presentation/surface.hpp"
#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"
#include "lc1/window.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Exercise actual draw calls and shader reads, not just pipeline construction.
std::vector<std::uint8_t> render(lc1::Device const &device, lc1::Renderer &renderer,
                                 lc1::FrameResources &frame, lc1::scene::FpsCamera &camera,
                                 std::span<lc1::scene::Light const> lights,
                                 std::span<lc1::DrawItem const> draws,
                                 vk::Extent2D extent = {64, 64})
{
    lc1::GpuImage output{device,
                         vk::Format::eR8G8B8A8Unorm,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    auto readback = device.allocator().createBuffer(
        {.size = extent.width * extent.height * 4, .usage = vk::BufferUsageFlagBits::eTransferDst},
        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
         .usage = vma::MemoryUsage::eAuto});
    camera.set_aspect_ratio(static_cast<float>(extent.width) / extent.height);
    lc1::one_time_submit(device, [&](lc1::CommandBuffer &wrapped) {
        auto const &commands = wrapped.raii();
        output.transition_layout(commands, vk::ImageLayout::eUndefined,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::PipelineStageFlagBits2::eNone, {},
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite);
        lc1::RenderTarget const target{&output.view(), extent};
        renderer.record(wrapped, frame, target, lc1::scene::Scene{camera, lights, draws});
        output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eTransferSrcOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eCopy,
                                 vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy const copy{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1},
        };
        commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                                   copy);
        vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                         .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                         .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                         .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
    });
    std::vector<std::uint8_t> pixels(extent.width * extent.height * 4);
    readback.getAllocation().copyToMemory(0, pixels.data(), pixels.size());
    return pixels;
}

void exercise(lc1::Device const &device, vk::SampleCountFlagBits samples)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, samples};
    lc1::GpuTexture texture{
        device, lc1::Image::load_from_file("runtime/assets/textures/prototype-white.png")};
    auto material =
        lc1::MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                          .base_color = {.texture = &texture}};
    std::array const vertices{
        lc1::Vertex{.position = {-2, 0, -2}, .uv = {0, 0}},
        lc1::Vertex{.position = {-2, 0, 2}, .uv = {0, 1}},
        lc1::Vertex{.position = {2, 0, 2}, .uv = {1, 1}},
        lc1::Vertex{.position = {2, 0, -2}, .uv = {1, 0}},
    };
    std::array<std::uint32_t, 6> const indices{0, 1, 2, 0, 2, 3};
    lc1::GpuMesh mesh{device, std::span<lc1::Vertex const>{vertices},
                      std::span<std::uint32_t const>{indices}};
    std::array draws{lc1::DrawItem{.mesh = &mesh, .material = &material, .depth_bias = false}};
    std::array const lights{
        lc1::scene::to_light(lc1::scene::DirectionalLight{.base = {}, .direction = {0, -1, 0}})};
    lc1::scene::FpsCamera camera;
    camera.set_zfar(50.0F);
    camera.set_position({0, 4, 5});
    camera.look_at({0, 0, 0});
    auto normal_frame = renderer.make_frame_resources(1);
    auto second_frame = renderer.make_frame_resources(1);

    auto const first_lit = render(device, renderer, normal_frame, camera, lights, draws);
    check(normal_frame.ray_query.scene && normal_frame.ray_query.visibility,
          "normal rendering did not prepare ray-query resources");
    check(render(device, renderer, second_frame, camera, lights, draws) == first_lit,
          "fresh frame slot changed a static hard-lit scene");

    // Rebuild the frame with empty input spans, then restore them on both slots.
    // Stale callbacks must not retain the previous draws or skip clearing/tone mapping.
    auto const empty = render(device, renderer, normal_frame, camera, {}, {});
    check(normal_frame.ray_query.built_instances.empty(), "empty frame retained shadow casters");
    check(empty != first_lit, "empty draw list retained the lit scene");
    // The first pixel lies outside this fixture's ground plane and supplies its
    // tone-mapped background color without depending on UNORM rounding.
    for (std::size_t i = 0; i < empty.size(); ++i)
        check(empty[i] == first_lit[i % 4], "empty frame did not clear the entire output");

    constexpr vk::Extent2D unlit_extent{64, 64};
    auto const unlit = render(device, renderer, second_frame, camera, {}, draws, unlit_extent);
    constexpr auto center =
        (unlit_extent.height / 2 * unlit_extent.width + unlit_extent.width / 2) * 4;
    check(unlit[center] == 0 && unlit[center + 1] == 0 && unlit[center + 2] == 0 &&
              unlit[center + 3] == 255,
          "frame without lights retained stale lighting or skipped geometry");
    check(render(device, renderer, normal_frame, camera, lights, draws) == first_lit,
          "restoring geometry after an empty frame changed the lit scene");
    check(normal_frame.ray_query.built_instances.size() == 1,
          "restoring geometry after an empty frame did not rebuild the TLAS");
    check(render(device, renderer, second_frame, camera, lights, draws) == first_lit,
          "restoring lights changed the lit scene");

    // Printed labels remain visible without entering the shadow-caster path.
    // Exercise an empty TLAS and invalidate history when caster membership changes.
    draws[0].casts_shadow = false;
    auto const no_caster = render(device, renderer, normal_frame, camera, lights, draws);
    check(no_caster == first_lit && normal_frame.ray_query.built_instances.empty(),
          "non-caster must remain visible without entering the TLAS");
    draws[0].casts_shadow = true;
    render(device, renderer, normal_frame, camera, lights, draws);
    check(normal_frame.ray_query.built_instances.size() == 1, "caster must rejoin the TLAS");

    // Resize both slots, then restore the original extent. Each render waits for
    // completion, so shared temporal resources are idle before resizing.
    auto const resized = render(device, renderer, second_frame, camera, lights, draws, {96, 48});
    check(render(device, renderer, normal_frame, camera, lights, draws, {96, 48}) == resized,
          "resized frame slots produced different static images");
    check(render(device, renderer, normal_frame, camera, lights, draws) == first_lit,
          "resizing changed a static hard-lit scene");
    check(render(device, renderer, second_frame, camera, lights, draws) == first_lit,
          "restoring the second frame slot changed a static hard-lit scene");

    // Reflect the symmetric ground plane to exercise winding and BLAS transforms.
    draws[0].model_matrix = glm::scale(glm::mat4{1}, glm::vec3{-1, 1, 1});
    render(device, renderer, normal_frame, camera, lights, draws);

    // An elevated patch casts a visible shadow on the ground. Toggling only its
    // caster membership must change pixels, catching a missing/incorrect TLAS lookup.
    auto occlusion_frame = renderer.make_frame_resources(2);
    std::array occlusion_draws{
        lc1::DrawItem{.mesh = &mesh, .material = &material, .depth_bias = false},
        lc1::DrawItem{.mesh = &mesh,
                      .material = &material,
                      .model_matrix = glm::scale(glm::translate(glm::mat4{1}, glm::vec3{0, 1, 0}),
                                                 glm::vec3{0.3F}),
                      .casts_shadow = false,
                      .depth_bias = false},
    };
    auto const unshadowed =
        render(device, renderer, occlusion_frame, camera, lights, occlusion_draws);
    occlusion_draws[1].casts_shadow = true;
    auto const shadowed =
        render(device, renderer, occlusion_frame, camera, lights, occlusion_draws);
    std::size_t darker_pixels = 0;
    for (std::size_t i = 0; i < shadowed.size(); i += 4) {
        int const before = unshadowed[i] + unshadowed[i + 1] + unshadowed[i + 2];
        int const after = shadowed[i] + shadowed[i + 1] + shadowed[i + 2];
        if (before > after + 30)
            ++darker_pixels;
    }
    check(darker_pixels > 10, "TLAS occluder did not produce a visible ray-query shadow");
    occlusion_draws[1].casts_shadow = false;
    check(render(device, renderer, occlusion_frame, camera, lights, occlusion_draws) == unshadowed,
          "removing the TLAS occluder did not restore the unshadowed image");
    device.wait_idle();
}

} // namespace

int main()
{
    try {
        lc1::VulkanLoader loader;
        lc1::Window window{64, 64, "shadow path GPU tests"};
        glfwHideWindow(window.handle());
        auto extensions = window.required_instance_extensions();
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        lc1::Instance instance{loader, std::move(extensions), {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        lc1::Surface surface{instance, window};
        auto device = lc1::test::make_render_device(instance, *surface.raii());
        exercise(device, vk::SampleCountFlagBits::e1);
        auto const samples = std::min(device.max_sample_count(), vk::SampleCountFlagBits::e4);
        if (samples != vk::SampleCountFlagBits::e1)
            exercise(device, samples);
        std::cout
            << "ray-query empty frames, caster changes, frame slots, resize and rendering passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
