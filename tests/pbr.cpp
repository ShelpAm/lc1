#include "character-input.hpp"
#include "render-device.hpp"

#include "lc1/game/character/ai-character-controller.hpp"
#include "lc1/game/character/character-model.hpp"
#include "lc1/game/character/character.hpp"
#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/game/physics/world-collision.hpp"
#include "lc1/game/session/save.hpp"
#include "lc1/game/world/game-world.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/gpu-model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/point-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/scene/model-instance.hpp"
#include "lc1/scene/model.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/renderer.hpp"

#include "lc1/game/camera/map-camera-controller.hpp"
#include "lc1/game/continent/view.hpp"

#include "lc1/platform/window.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/presentation/surface.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"

#include <glm/gtc/packing.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void check(bool condition, std::string const &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void near(float actual, float expected, std::string const &message, float tolerance = 0.003F)
{
    check(std::isfinite(actual) &&
              std::abs(actual - expected) <= tolerance * std::max(1.0F, expected),
          message + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
}

void rejects(auto &&operation, std::string const &message)
{
    bool rejected = false;
    try {
        operation();
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, message);
}

struct Capture {
    vk::Extent2D extent;
    std::vector<std::uint8_t> output;
    std::vector<glm::vec4> hdr;

    glm::vec3 center() const { return hdr[(extent.height / 2) * extent.width + extent.width / 2]; }
};

Capture capture(lc1::Device const &device, lc1::Renderer &renderer, lc1::FrameResources &frame,
                lc1::scene::FpsCamera &camera, lc1::scene::Scene const &scene,
                vk::Extent2D extent = {.width = 65, .height = 65},
                lc1::OutputSettings settings = {}, vk::Format format = vk::Format::eR8G8B8A8Unorm,
                lc1::TextFrameInput text = {}, double *gpu_ms = nullptr)
{
    std::optional<vk::raii::QueryPool> timing;
    if (gpu_ms)
        timing.emplace(device.raii().createQueryPool(
            {.queryType = vk::QueryType::eTimestamp, .queryCount = 2}));
    lc1::GpuImage output{device,
                         format,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    auto const ldr_bytes = vk::DeviceSize{extent.width} * extent.height * 4;
    // RGBA16F copies require an offset aligned to their 8-byte texel size.
    auto const hdr_offset = (ldr_bytes + 7) & ~vk::DeviceSize{7};
    auto readback = device.allocator().createBuffer(
        {.size = hdr_offset + ldr_bytes * 2, .usage = vk::BufferUsageFlagBits::eTransferDst},
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
        if (timing) {
            commands.resetQueryPool(**timing, 0, 2);
            commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, **timing, 0);
        }
        renderer.record(wrapped, frame, {&output.view(), extent}, scene, camera, settings, text);
        if (timing)
            commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, **timing, 1);
        output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eTransferSrcOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eCopy,
                                 vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {.width = extent.width, .height = extent.height, .depth = 1}};
        commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                                   region);
        frame.hdr_image->transition_layout(
            commands, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        region.bufferOffset = hdr_offset;
        commands.copyImageToBuffer(*frame.hdr_image->raii(), vk::ImageLayout::eTransferSrcOptimal,
                                   *readback, region);
        frame.hdr_image->transition_layout(
            commands, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        vk::MemoryBarrier2 const host_barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                              .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                              .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                              .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(host_barrier));
    });
    if (timing) {
        std::array<std::uint64_t, 2> timestamps{};
        check(timing->getResults(0, 2, sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t),
                                 vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait) ==
                  vk::Result::eSuccess,
              "GPU timestamps available after submission completion");
        *gpu_ms = static_cast<double>(timestamps[1] - timestamps[0]) *
                  device.raii_physical().getProperties().limits.timestampPeriod / 1e6;
    }
    Capture result{extent, std::vector<std::uint8_t>(ldr_bytes), {}};
    readback.getAllocation().copyToMemory(0, result.output.data(), ldr_bytes);
    std::vector<std::uint16_t> half(extent.width * extent.height * 4);
    readback.getAllocation().copyToMemory(hdr_offset, half.data(),
                                          half.size() * sizeof(std::uint16_t));
    for (std::size_t i = 0; i < half.size(); i += 4) {
        glm::vec4 color;
        for (int c = 0; c < 4; ++c) {
            color[c] = glm::unpackHalf1x16(half[i + c]);
            check(std::isfinite(color[c]), "nonfinite HDR pixel");
        }
        result.hdr.push_back(color);
    }
    return result;
}

Capture capture(lc1::Device const &device, lc1::Renderer &renderer, lc1::FrameResources &frame,
                lc1::scene::FpsCamera &camera, std::span<lc1::scene::Light const> lights,
                std::span<lc1::scene::SceneModel const> draws,
                vk::Extent2D extent = {.width = 65, .height = 65},
                lc1::OutputSettings settings = {}, vk::Format format = vk::Format::eR8G8B8A8Unorm,
                lc1::TextFrameInput text = {})
{
    lc1::scene::Scene scene;
    for (auto const &light : lights)
        scene.add_light(light);
    for (auto const &draw : draws)
        scene.add_model(draw);
    return capture(device, renderer, frame, camera, scene, extent, settings, format, text);
}

float srgb(float value)
{
    return value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

void check_tone_map(Capture const &result, float exposure, bool encoded)
{
    for (std::size_t i = 0; i < result.hdr.size(); ++i)
        for (int c = 0; c < 3; ++c) {
            float const value = result.hdr[i][c] * exposure;
            float expected = value / (1.0F + value);
            if (encoded)
                expected = srgb(expected);
            near(result.output[i * 4 + c] / 255.0F, expected, "tone mapping after HDR resolve",
                 2.0F / 255);
        }
}

void ddgi_candidate_tests(lc1::Device const &device, lc1::FrameResources &frame)
{
    auto const extent = frame.ddgi_resolved->extent();
    lc1::GraphicsShaders reference{device, "shaders/ddgi-resolve.spv", "ddgiVert",
                                   "ddgiResolveReference"};
    lc1::GraphicsShaders cached{device, "shaders/ddgi-resolve.spv", "ddgiVert", "ddgiResolve"};
    std::array shaders{&reference, &cached};
    auto make_target = [&] {
        return lc1::GpuImage{device,
                             vk::Format::eR32G32B32A32Sfloat,
                             extent,
                             1,
                             vk::SampleCountFlagBits::e1,
                             vk::ImageUsageFlagBits::eColorAttachment |
                                 vk::ImageUsageFlagBits::eTransferSrc,
                             vk::ImageAspectFlagBits::eColor};
    };
    std::array targets{make_target(), make_target()};
    auto const bytes = vk::DeviceSize{extent.width} * extent.height * sizeof(glm::vec4);
    auto readback = device.allocator().createBuffer(
        {.size = bytes * 2, .usage = vk::BufferUsageFlagBits::eTransferDst},
        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
         .usage = vma::MemoryUsage::eAuto});
    auto queries =
        device.raii().createQueryPool({.queryType = vk::QueryType::eTimestamp, .queryCount = 32});
    lc1::one_time_submit(device, [&](lc1::CommandBuffer &cmd) {
        auto const &commands = cmd.raii();
        cmd.bind_descriptor_heap(*frame.descriptor_heap);
        cmd.push_data(std::span{&*frame.ddgi_index, 1});
        commands.resetQueryPool(*queries, 0, 32);
        for (std::uint32_t i = 0; i < 16; ++i) {
            // Reverse order every pair to reduce ordering and clock-ramp bias.
            std::uint32_t const mode = (i & 1U) ^ ((i / 2U) & 1U);
            auto &target = targets[mode];
            target.transition_layout(commands, vk::ImageLayout::eUndefined,
                                     vk::ImageLayout::eColorAttachmentOptimal,
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                     vk::AccessFlagBits2::eColorAttachmentWrite);
            commands.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *queries, i * 2);
            vk::RenderingAttachmentInfo attachment{.imageView = *target.view(),
                                                   .imageLayout =
                                                       vk::ImageLayout::eColorAttachmentOptimal,
                                                   .loadOp = vk::AttachmentLoadOp::eDontCare,
                                                   .storeOp = vk::AttachmentStoreOp::eStore};
            vk::RenderingInfo rendering{.renderArea = {{0, 0}, extent}, .layerCount = 1};
            rendering.setColorAttachments(attachment);
            commands.beginRendering(rendering);
            shaders[mode]->bind_to_command_buffer(cmd);
            lc1::set_graphics_state(commands, {});
            commands.setViewportWithCount(vk::Viewport{.width = static_cast<float>(extent.width),
                                                       .height = static_cast<float>(extent.height),
                                                       .minDepth = 0,
                                                       .maxDepth = 1});
            commands.setScissorWithCount(vk::Rect2D{{0, 0}, extent});
            commands.draw(3, 1, 0, 0);
            commands.endRendering();
            commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *queries,
                                     i * 2 + 1);
        }
        for (std::size_t i = 0; i < targets.size(); ++i) {
            targets[i].transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                         vk::ImageLayout::eTransferSrcOptimal,
                                         vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                         vk::AccessFlagBits2::eColorAttachmentWrite,
                                         vk::PipelineStageFlagBits2::eCopy,
                                         vk::AccessFlagBits2::eTransferRead);
            vk::BufferImageCopy region{
                .bufferOffset = bytes * i,
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .imageExtent = {extent.width, extent.height, 1}};
            commands.copyImageToBuffer(*targets[i].raii(), vk::ImageLayout::eTransferSrcOptimal,
                                       *readback, region);
        }
        vk::MemoryBarrier2 host{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(host));
    });
    std::array<std::uint64_t, 32> stamps{};
    check(queries.getResults(0, 32, sizeof(stamps), stamps.data(), sizeof(std::uint64_t),
                             vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait) ==
              vk::Result::eSuccess,
          "candidate timestamps available");
    std::array<double, 2> milliseconds{};
    double const scale = device.raii_physical().getProperties().limits.timestampPeriod / 1e6;
    for (unsigned i = 0; i < 16; ++i)
        milliseconds[(i & 1U) ^ ((i / 2U) & 1U)] += (stamps[i * 2 + 1] - stamps[i * 2]) * scale / 8;
    std::vector<glm::vec4> values(extent.width * extent.height * 2);
    readback.getAllocation().copyToMemory(0, values.data(), bytes * 2);
    float maximum_error = 0;
    auto const count = extent.width * extent.height;
    for (std::size_t i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c)
            maximum_error = std::max(maximum_error, std::abs(values[i][c] - values[i + count][c]));
    check(maximum_error < 1e-5F, "candidate cache must preserve the full emission integral");
    std::cout << "DDGI resolve A/B " << extent.width << 'x' << extent.height << ": full "
              << milliseconds[0] << " ms, spatial cache " << milliseconds[1]
              << " ms, max RGB error " << maximum_error << std::endl;
}

void ddgi_tests(lc1::Device const &device)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, vk::SampleCountFlagBits::e1};
    std::array frames{renderer.make_frame_resources(4), renderer.make_frame_resources(4)};
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 4});
    camera.look_at({0, 0, 0});
    camera.set_zfar(100.0F);
    std::array const vertices{
        lc1::Vertex{.position = {-20, -20, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
        lc1::Vertex{.position = {20, -20, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
        lc1::Vertex{.position = {20, 20, 0}, .normal = {0, 0, 1}, .uv = {1, 1}},
        lc1::Vertex{.position = {-20, 20, 0}, .normal = {0, 0, 1}, .uv = {0, 1}}};
    std::array<std::uint16_t, 6> const indices{0, 1, 2, 0, 2, 3};
    lc1::scene::GpuModel plane{lc1::GpuMesh{device, std::span<lc1::Vertex const>{vertices},
                                            std::span<std::uint16_t const>{indices}}};
    std::array<std::uint32_t, 6> const wide_indices{0, 1, 2, 0, 2, 3};
    lc1::scene::GpuModel emitter_plane{lc1::GpuMesh{device, std::span<lc1::Vertex const>{vertices},
                                                    std::span<std::uint32_t const>{wide_indices}}};
    lc1::MaterialInfo receiver;
    receiver.parameters.metallic_factor = 0;
    receiver.parameters.roughness_factor = 1;
    receiver.parameters.base_color_factor = {0.5F, 0.5F, 0.5F, 1};
    lc1::scene::Scene scene;
    auto *receiver_model = scene.add_model({.model = &plane, .material = &receiver});
    unsigned frame = 0;
    auto render = [&] {
        return capture(device, renderer, frames[frame++ % 2], camera, scene,
                       {.width = 33, .height = 33})
            .center();
    };
    near(render().r, 0, "DDGI disabled has no ambient floor");
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.sky_radiance = {0.08F, 0.04F, 0.02F}});
    auto sky = render();
    // Uniform sky has irradiance pi*L on a fully exposed normal. The diffuse
    // receiver reflects albedo*L; tolerate the finite 64-ray/probe estimate.
    near(sky.r, 0.04F, "DDGI uniform sky energy", 0.008F);
    near(sky.g, 0.02F, "DDGI sky RGB", 0.004F);
    near(sky.b, 0.01F, "DDGI sky RGB", 0.002F);
    for (int i = 0; i < 12; ++i)
        sky = render();
    near(sky.r, 0.04F, "DDGI two frame slots share probe history", 0.008F);
    scene.set_diffuse_gi(
        lc1::scene::DiffuseGi{.center = {4, 0, 0}, .sky_radiance = {0.08F, 0.04F, 0.02F}});
    for (int i = 0; i < 8; ++i)
        sky = render();
    near(sky.r, 0.04F, "DDGI scroll retains world-aligned probes", 0.008F);

    // Move the receiver and camera together while keeping the GI center fixed.
    // Every sample outside the old +/-30m volume must still receive sky light.
    // Sweep through all cascade boundaries, including the formerly dark far scene.
    for (float const x : {20.0F, 26.0F, 28.0F, 30.0F, 32.0F, 36.0F, 80.0F, 112.0F, 120.0F, 128.0F,
                          200.0F, 448.0F, 480.0F, 512.0F, 800.0F}) {
        receiver_model->world_transform = glm::translate(glm::mat4{1}, glm::vec3{x, 0, 0});
        camera.set_position({x, 0, 4});
        camera.look_at({x, 0, 0});
        auto const value = render();
        near(value.r, 0.04F, "DDGI cascades keep far receivers lit at x=" + std::to_string(x),
             0.01F);
    }
    // Crossing a grid-cell boundary must retain coarse coverage while fresh near
    // cells fill; this also catches histories accidentally copied across cascades.
    scene.set_diffuse_gi(
        lc1::scene::DiffuseGi{.center = {260, 0, 0}, .sky_radiance = {0.08F, 0.04F, 0.02F}});
    for (int i = 0; i < 8; ++i)
        near(render().r, 0.04F, "DDGI cascade scrolling preserves far light", 0.01F);
    receiver_model->world_transform = glm::mat4{1};
    camera.set_position({0, 0, 4});
    camera.look_at({0, 0, 0});
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.sky_radiance = {0.08F, 0.04F, 0.02F}});

    scene.set_diffuse_gi(std::nullopt);
    near(render().r, 0, "DDGI disable does not retain irradiance");
    lc1::MaterialInfo emission;
    emission.parameters.metallic_factor = 0;
    std::array<std::uint8_t, 4> const red_pixel{255, 0, 0, 255};
    lc1::GpuTexture emission_texture{
        device, {.width = 1, .height = 1}, red_pixel, lc1::TextureColorSpace::Srgb};
    emission.emissive.texture = &emission_texture;
    emission.parameters.emissive_factor = {2, 2, 2};
    auto const facing_receiver = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
    auto *emitter = scene.add_model(
        {.model = &emitter_plane,
         .material = &emission,
         .world_transform = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 5}) * facing_receiver});
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{});
    near(render().r, 0, "default DDGI excludes offscreen emissive lighting");
    scene.set_diffuse_gi(std::nullopt);
    render();
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{
        .emissive_lighting = true}); // Zero sky: only the emitter can illuminate.
    auto bounced = render();
    check(bounced.r > 0.2F, "DDGI transfers offscreen emitter light to a diffuse receiver");
    near(bounced.g, 0, "DDGI emission bounce preserves color", 0.002F);
    // A static one-metre LED must not blink when the camera and scene are fixed.
    emitter->world_transform = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 5}) * facing_receiver *
                               glm::scale(glm::mat4{1}, glm::vec3{0.025F});
    scene.set_diffuse_gi(std::nullopt);
    render();
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.emissive_lighting = true});
    float minimum = std::numeric_limits<float>::max();
    float maximum = 0;
    double total = 0;
    for (int i = 0; i < 512; ++i) {
        auto const value = render().r;
        if (i >= 256) {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            total += value;
        }
    }
    std::cout << "Static LED: min=" << minimum << " max=" << maximum << " mean=" << total / 256
              << std::endl;
    check(minimum > 0.002F && maximum - minimum < 0.001F,
          "static small LED must remain lit without temporal flashes");
    float previous_brightness = std::numeric_limits<float>::max();
    for (float const distance : {1.0F, 2.0F, 3.0F, 3.9F, 4.1F, 5.0F, 7.9F, 8.1F}) {
        emitter->world_transform = glm::translate(glm::mat4{1}, glm::vec3{0, 0, distance}) *
                                   facing_receiver * glm::scale(glm::mat4{1}, glm::vec3{0.025F});
        scene.set_diffuse_gi(std::nullopt);
        render();
        scene.set_diffuse_gi(lc1::scene::DiffuseGi{.emissive_lighting = true});
        float const brightness = render().r;
        // Independent numerical surface integral for a 1x1 Lambert emitter.
        // Albedo .5 times emission 2 equals one; dA is 1/64^2.
        double reference = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                double const u = (x + 0.5) / 64.0 - 0.5;
                double const v = (y + 0.5) / 64.0 - 0.5;
                double const d2 = distance * distance;
                double const r2 = u * u + v * v + d2;
                reference += d2 / (glm::pi<double>() * r2 * r2 * 4096.0);
            }
        check(brightness > 0 && brightness < previous_brightness * 1.1F,
              "LED distance sweep has no dark gaps or large brightness jumps");
        near(brightness, static_cast<float>(reference),
             "LED surface integral at distance " + std::to_string(distance),
             static_cast<float>(reference * 0.3));
        previous_brightness = brightness;
        std::cout << "LED distance " << distance << ": " << brightness << std::endl;
    }
    emitter->world_transform = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 2}) * facing_receiver *
                               glm::scale(glm::mat4{1}, glm::vec3{-0.025F, 0.025F, 0.025F});
    scene.set_diffuse_gi(std::nullopt);
    render();
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.emissive_lighting = true});
    near(render().r, 0.0735F, "mirrored LED preserves its emitting side", 0.005F);
    emission.parameters.emissive_factor = {0, 0, 0};
    check(render().r < 0.001F, "near LED switch-off does not retain a frozen bright value");
    emission.parameters.emissive_factor = {2, 2, 2};
    emitter->world_transform = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 5}) * facing_receiver;
    scene.set_diffuse_gi(std::nullopt);
    render();
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.emissive_lighting = true});
    render();
    // Exclude the receiver from shadow rays to isolate the offscreen wall's
    // reflected direct light. Its visible normal faces away from the sun.
    receiver_model->casts_shadow = false;
    emission.parameters.emissive_factor = {0, 0, 0};
    emission.parameters.base_color_factor = {0.9F, 0.1F, 0.1F, 1};
    auto *sun = scene.add_light(lc1::scene::to_light(
        lc1::scene::DirectionalLight{.base = {.intensity = 3}, .direction = {0, 0, 1}}));
    scene.set_diffuse_gi(std::nullopt);
    near(render().r, 0, "back-facing receiver has no direct sun contribution");
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{});
    auto reflected_sun = render();
    check(reflected_sun.r > 0.05F && reflected_sun.r > reflected_sun.g * 4,
          "DDGI transfers colored diffuse sunlight from an offscreen wall");
    scene.remove_light(sun);
    receiver_model->casts_shadow = true;
    emission.parameters.emissive_factor = {2, 2, 2};
    scene.set_diffuse_gi(std::nullopt);
    render();
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.emissive_lighting = true});
    render();
    // The blocker is behind the camera: visible receiver geometry is unchanged.
    lc1::MaterialInfo black;
    black.parameters.base_color_factor = {0, 0, 0, 1};
    black.parameters.metallic_factor = 0;
    scene.add_model(
        {.model = &plane,
         .material = &black,
         .world_transform = glm::translate(glm::mat4{1}, glm::vec3{0, 0, 4.5F}) * facing_receiver});
    for (int i = 0; i < 256; ++i)
        bounced = render();
    check(bounced.r < 0.03F, "DDGI opaque blocker removes the offscreen emission bounce: " +
                                 std::to_string(bounced.r));
    scene.remove_model(emitter);
    scene.set_diffuse_gi(std::nullopt);
    render();
    scene.set_diffuse_gi(lc1::scene::DiffuseGi{.emissive_lighting = true});
    near(render().r, 0, "DDGI re-enable initializes a black unlit scene");
    device.wait_idle();
    std::cout << "DDGI sky, emission bounce, occlusion, scrolling and frame slots passed\n";
}

void material_tests(lc1::Device const &device, vk::SampleCountFlagBits samples)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, samples};
    auto frame = renderer.make_frame_resources(1);
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 4});
    camera.look_at({0, 0, 0});
    camera.set_zfar(1000.0F);
    std::array const vertices{
        lc1::Vertex{.position = {-10, -10, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
        lc1::Vertex{.position = {10, -10, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
        lc1::Vertex{.position = {10, 10, 0}, .normal = {0, 0, 1}, .uv = {1, 1}},
        lc1::Vertex{.position = {-10, 10, 0}, .normal = {0, 0, 1}, .uv = {0, 1}}};
    std::array<std::uint32_t, 6> const indices{0, 1, 2, 0, 2, 3};
    lc1::scene::GpuModel mesh{lc1::GpuMesh{device, std::span<lc1::Vertex const>{vertices},
                                           std::span<std::uint32_t const>{indices}}};
    auto const sun = lc1::scene::to_light(
        lc1::scene::DirectionalLight{.base = {.intensity = 1}, .direction = {0, 0, -1}});
    std::array lights{sun};
    auto render_material = [&](lc1::MaterialInfo const &info,
                               std::span<lc1::scene::Light const> active_lights,
                               lc1::OutputSettings settings = {}) {
        auto material = info;
        std::array draws{
            lc1::scene::SceneModel{.model = &mesh, .material = &material, .depth_bias = false}};
        return capture(device, renderer, frame, camera, active_lights, draws,
                       {.width = 65, .height = 65}, settings);
    };
    lc1::MaterialInfo info;
    info.parameters.base_color_factor = {0.8F, 0.2F, 0.05F, 1};
    info.parameters.roughness_factor = 1;
    for (float const metallic : {0.0F, 0.5F, 1.0F}) {
        info.parameters.metallic_factor = metallic;
        auto const result = render_material(info, lights);
        // Closed-form normal-incidence reference at roughness 1; independent of
        // shader's optimized D/G implementation. The 1x center ray hits the origin.
        for (int c = 0; c < 3; ++c) {
            float const base = info.parameters.base_color_factor[c];
            float const f0 = 0.04F * (1 - metallic) + base * metallic;
            float const expected = ((1 - f0) * (1 - metallic) * base + f0 / 4) / glm::pi<float>();
            near(result.center()[c], expected, "normal-incidence BRDF");
        }
        check_tone_map(result, 1, false);
    }
    auto const dark = render_material(info, {});
    near(dark.center().r, 0, "unlit metal has no fixed ambient");
    info.parameters.emissive_factor = {4, 2, 0.5F};
    auto const emission = render_material(info, {});
    near(emission.center().r, 4, "HDR emission must exceed one");
    near(emission.center().b, 0.5F, "emissive fallback is white");
    check_tone_map(emission, 1, false);
    check_tone_map(render_material(info, {}, {.exposure = 2}), 2, false);
    check_tone_map(render_material(info, {}, {.encoding = lc1::OutputEncoding::Srgb}), 1, true);
    info.parameters.emissive_factor = {0, 0, 0};

    std::array<std::uint8_t, 4> const gray{128, 128, 128, 255};
    lc1::GpuTexture color{device, {.width = 1, .height = 1}, gray, lc1::TextureColorSpace::Srgb};
    lc1::GpuTexture data{device, {.width = 1, .height = 1}, gray, lc1::TextureColorSpace::Linear};
    info.parameters.emissive_factor = {1, 1, 1};
    info.emissive.texture = &color;
    float const decoded = std::pow((128.0F / 255 + 0.055F) / 1.055F, 2.4F);
    near(render_material(info, {}).center().r, decoded, "sRGB texture decodes once");
    info.emissive.texture = &data;
    rejects([&] { lc1::validate_material(info); }, "linear emissive texture must be rejected");
    info.emissive.texture = nullptr;
    info.parameters.emissive_factor = {0, 0, 0};

    std::array<std::uint8_t, 4> const packed{231, 128, 64, 255};
    lc1::GpuTexture mr{device, {.width = 1, .height = 1}, packed, lc1::TextureColorSpace::Linear};
    info.parameters.roughness_factor = 0.8F;
    info.parameters.metallic_factor = 0.7F;
    info.metallic_roughness.texture = &mr;
    auto const textured = render_material(info, lights).center();
    info.metallic_roughness.texture = nullptr;
    info.parameters.roughness_factor *= 128.0F / 255;
    info.parameters.metallic_factor *= 64.0F / 255;
    auto const factored = render_material(info, lights).center();
    for (int c = 0; c < 3; ++c)
        near(textured[c], factored[c], "linear MR G/B channels and factors");
    info.base_color.texture = &color;
    auto const base_textured = render_material(info, lights).center();
    info.base_color.texture = nullptr;
    info.parameters.base_color_factor *= glm::vec4{decoded, decoded, decoded, 1};
    auto const base_factored = render_material(info, lights).center();
    for (int c = 0; c < 3; ++c)
        near(base_textured[c], base_factored[c], "base texture times factor");

    info.parameters.roughness_factor = 0;
    auto const sharp = render_material(info, lights);
    check(sharp.center().r > 1, "sharp highlights must remain HDR");
    info.parameters.roughness_factor = 0.045F;
    near(render_material(info, lights).center().r, sharp.center().r, "roughness lower bound");
    info.parameters.roughness_factor = 1;
    auto const direct = render_material(info, lights).center();
    std::array const local_lights{
        lc1::scene::to_light(
            lc1::scene::PointLight{.base = {.intensity = 16}, .position = {0, 0, 4}}),
        lc1::scene::to_light(lc1::scene::SpotLight{.base = {.intensity = 16},
                                                   .position = {0, 0, 4},
                                                   .direction = {0, 0, -1},
                                                   .inner_angle = 0.2F,
                                                   .outer_angle = 0.5F}),
        lc1::scene::to_light(lc1::scene::SphereLight{
            .base = {.intensity = 16}, .position = {0, 0, 4}, .radius = 0})};
    for (auto const &light : local_lights)
        near(render_material(info, std::span{&light, 1}).center().r, direct.r,
             "light attenuation applied once");
    lights[0].direction = {0, 0, 1};
    near(render_material(info, lights).center().r, 0, "back light contributes no BRDF");
    lights[0] = sun;
    camera.set_position({100, 0, 0.1F});
    camera.look_at({0, 0, 0});
    render_material(info, lights); // Every captured pixel is checked for finite HDR.

    info.parameters.roughness_factor = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { lc1::validate_material(info); }, "NaN material must be rejected");
    info.parameters.roughness_factor = 1;
    rejects([&] { render_material(info, {}, {.exposure = 0}); },
            "invalid exposure must be rejected");
}

void mutable_material_tests(lc1::Device const &device)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, vk::SampleCountFlagBits::e1};
    auto first_frame = renderer.make_frame_resources(2);
    auto second_frame = renderer.make_frame_resources(2);
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 4});
    camera.look_at({0, 0, 0});
    std::array const vertices{
        lc1::Vertex{.position = {-0.5F, -0.5F, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
        lc1::Vertex{.position = {0.5F, -0.5F, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
        lc1::Vertex{.position = {0.5F, 0.5F, 0}, .normal = {0, 0, 1}, .uv = {1, 1}},
        lc1::Vertex{.position = {-0.5F, 0.5F, 0}, .normal = {0, 0, 1}, .uv = {0, 1}},
    };
    std::array<std::uint32_t, 6> const indices{0, 1, 2, 0, 2, 3};
    lc1::scene::GpuModel mesh{lc1::GpuMesh{device, std::span<lc1::Vertex const>{vertices},
                                           std::span<std::uint32_t const>{indices}}};
    lc1::MaterialInfo left{.parameters = {.emissive_factor = {1, 0, 0}}};
    lc1::MaterialInfo right{.parameters = {.emissive_factor = {0, 1, 0}}};
    std::array draws{
        lc1::scene::SceneModel{.model = &mesh,
                               .material = &left,
                               .world_transform =
                                   glm::translate(glm::mat4{1}, glm::vec3{-0.8F, 0, 0}),
                               .casts_shadow = false,
                               .depth_bias = false},
        lc1::scene::SceneModel{.model = &mesh,
                               .material = &right,
                               .world_transform =
                                   glm::translate(glm::mat4{1}, glm::vec3{0.8F, 0, 0}),
                               .casts_shadow = false,
                               .depth_bias = false},
    };
    auto check_pair = [&](lc1::FrameResources &frame, glm::vec3 expected_left,
                          glm::vec3 expected_right) {
        auto const result = capture(device, renderer, frame, camera, {}, draws);
        for (int channel = 0; channel < 3; ++channel) {
            near(result.hdr[32 * 65 + 16][channel], expected_left[channel], "left draw material");
            near(result.hdr[32 * 65 + 48][channel], expected_right[channel], "right draw material");
        }
    };
    check_pair(first_frame, {1, 0, 0}, {0, 1, 0});
    left.parameters.emissive_factor = {0, 0, 1};
    check_pair(second_frame, {0, 0, 1}, {0, 1, 0});
    check_pair(first_frame, {0, 0, 1}, {0, 1, 0});
    draws[1].material = &left; // Shared CPU material, independent draw buffers.
    left.parameters.emissive_factor = {1, 0.5F, 0};
    check_pair(first_frame, {1, 0.5F, 0}, {1, 0.5F, 0});
    std::array<std::uint8_t, 4> const cyan{0, 255, 255, 255};
    lc1::GpuTexture texture{device, {1, 1}, cyan, lc1::TextureColorSpace::Srgb};
    left.parameters.emissive_factor = {1, 1, 1};
    left.emissive.texture = &texture;
    check_pair(first_frame, {0, 1, 1}, {0, 1, 1});
    left.emissive.texture = nullptr;
    check_pair(first_frame, {1, 1, 1}, {1, 1, 1});
    auto small_frame = renderer.make_frame_resources(1);
    rejects([&] { capture(device, renderer, small_frame, camera, {}, draws); },
            "draw capacity overflow must fail before upload");
}

void normal_map_tests(lc1::Device const &device, vk::SampleCountFlagBits samples)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, samples};
    auto frame = renderer.make_frame_resources(1);
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 4});
    camera.look_at({0, 0, 0});
    camera.set_zfar(100);
    std::vector<lc1::Vertex> vertices{
        {.position = {-10, -10, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
        {.position = {10, -10, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
        {.position = {10, 10, 0}, .normal = {0, 0, 1}, .uv = {1, 1}},
        {.position = {-10, 10, 0}, .normal = {0, 0, 1}, .uv = {0, 1}}};
    std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    lc1::scene::GpuModel no_tangents{lc1::GpuMesh{device, vertices, indices}};
    lc1::generate_tangents(vertices, indices);
    lc1::scene::GpuModel mesh{lc1::GpuMesh{device, vertices, indices}};
    check(mesh.meshes()[0].has_tangents() && !no_tangents.meshes()[0].has_tangents(),
          "mesh tangent availability");
    std::array const tilt_pixel = std::array<std::uint8_t, 4>{204, 179, 229, 255};
    std::array<std::uint8_t, 4> const flat_pixel{128, 128, 255, 255};
    lc1::GpuTexture tilt{
        device, {.width = 1, .height = 1}, tilt_pixel, lc1::TextureColorSpace::Linear};
    lc1::GpuTexture flat{
        device, {.width = 1, .height = 1}, flat_pixel, lc1::TextureColorSpace::Linear};
    lc1::GpuTexture wrong_space{device, {.width = 1, .height = 1}, tilt_pixel};
    lc1::MaterialInfo info;
    info.parameters = {.base_color_factor = {0.45F, 0.3F, 0.18F, 1},
                       .metallic_factor = 0,
                       .roughness_factor = 0.7F};
    std::array lights{lc1::scene::to_light(lc1::scene::DirectionalLight{
        .base = {.intensity = 2}, .direction = glm::normalize(glm::vec3{-0.3F, -0.4F, -1})})};
    auto render = [&](lc1::scene::GpuModel const &geometry, lc1::MaterialInfo const &parameters,
                      glm::mat4 transform = glm::mat4{1}) {
        auto material = parameters;
        std::array draws{lc1::scene::SceneModel{.model = &geometry,
                                                .material = &material,
                                                .world_transform = transform,
                                                .depth_bias = false}};
        return capture(device, renderer, frame, camera, lights, draws);
    };
    auto const baseline = render(no_tangents, info).center();
    info.normal.texture = &flat;
    auto const flat_result = render(mesh, info).center();
    for (int c = 0; c < 3; ++c)
        near(flat_result[c], baseline[c], "flat normal map (RGBA8 neutral quantization)");
    info.normal.texture = &tilt;
    info.parameters.normal_scale = 0;
    auto const disabled = render(mesh, info).center();
    for (int c = 0; c < 3; ++c)
        near(disabled[c], baseline[c], "normal scale zero", 1e-6F);
    info.parameters.normal_scale = 1;
    rejects([&] { render(no_tangents, info); }, "normal map requires a valid tangent mesh");
    info.normal.texture = &wrong_space;
    rejects([&] { lc1::validate_material(info); }, "normal texture must be linear");
    info.normal.texture = &tilt;

    auto reference_case = [&](std::vector<lc1::Vertex> source, glm::mat4 model, float scale) {
        auto source_indices = indices;
        lc1::generate_tangents(source, source_indices);
        lc1::scene::GpuModel source_mesh{lc1::GpuMesh{device, source, source_indices}};
        auto const linear = glm::mat3{model};
        auto const n = glm::normalize(glm::transpose(glm::inverse(linear)) * source[0].normal);
        auto const raw_t = linear * glm::vec3{source[0].tangent};
        auto const t = glm::normalize(raw_t - n * glm::dot(n, raw_t));
        auto const sign = source[0].tangent.w * (glm::determinant(linear) < 0 ? -1.0F : 1.0F);
        auto const b = glm::cross(n, t) * sign;
        glm::vec3 sampled{tilt_pixel[0], tilt_pixel[1], tilt_pixel[2]};
        sampled = sampled / 255.0F * 2.0F - 1.0F;
        sampled.x *= scale;
        sampled.y *= scale;
        auto const reference_normal = glm::normalize(t * sampled.x + b * sampled.y + n * sampled.z);
        // Independent reference: bake that world-space shading normal into the mesh
        // and use the ordinary no-map shader path. Geometry/lighting remain identical.
        auto reference_vertices = source;
        for (auto &vertex : reference_vertices) {
            vertex.position = glm::vec3{model * glm::vec4{vertex.position, 1}};
            vertex.normal = reference_normal;
            vertex.tangent = glm::vec4{0};
        }
        auto reference_indices = source_indices;
        if (glm::determinant(linear) < 0)
            for (std::size_t i = 0; i < reference_indices.size(); i += 3)
                std::swap(reference_indices[i + 1], reference_indices[i + 2]);
        lc1::scene::GpuModel reference_mesh{
            lc1::GpuMesh{device, reference_vertices, reference_indices}};
        info.parameters.normal_scale = scale;
        auto const actual = render(source_mesh, info, model).center();
        auto reference_info = info;
        reference_info.normal.texture = nullptr;
        auto const expected = render(reference_mesh, reference_info).center();
        for (int c = 0; c < 3; ++c)
            near(actual[c], expected[c], "TBN mapping vs world-normal reference", 0.005F);
    };
    reference_case(vertices, glm::mat4{1}, 1);
    reference_case(vertices, glm::scale(glm::mat4{1}, glm::vec3{-1, 1, 1}), 1);
    auto const transform = glm::rotate(glm::mat4{1}, 0.35F, glm::vec3{0, 1, 0}) *
                           glm::scale(glm::mat4{1}, glm::vec3{-2, 0.65F, 1.3F});
    reference_case(vertices, transform, 2);
    // A diagonal tangent distinguishes linear T transformation from the incorrect
    // inverse-transpose choice, which axis-aligned tangents alone would not expose.
    auto diagonal_uv = vertices;
    for (auto &vertex : diagonal_uv)
        vertex.uv = {vertex.uv.x + vertex.uv.y, vertex.uv.y - vertex.uv.x};
    reference_case(diagonal_uv, transform, 2);
    auto mirrored_uv = vertices;
    for (auto &vertex : mirrored_uv)
        vertex.uv.x = 1 - vertex.uv.x;
    reference_case(mirrored_uv, glm::mat4{1}, 1);
    reference_case(mirrored_uv, transform, 2);
    info.parameters.normal_scale = 2;
    lights[0].direction = glm::normalize(glm::vec3{-1, 0, 0.1F});
    near(render(mesh, info).center().r, 0,
         "normal map must not light the geometric back hemisphere");
}

void model_animation_test(lc1::Device const &device)
{
    lc1::scene::GpuModel const model{
        device, [] {
            std::vector<lc1::Vertex> vertices{
                {.position = {-0.3F, -0.3F, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
                {.position = {0.3F, -0.3F, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
                {.position = {0.3F, 0.3F, 0}, .normal = {0, 0, 1}, .uv = {1, 1}},
                {.position = {-0.3F, 0.3F, 0}, .normal = {0, 0, 1}, .uv = {0, 1}}};
            std::vector<lc1::scene::Mesh> meshes;
            meshes.emplace_back(vertices, std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
            meshes.emplace_back(std::move(vertices), std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
            return lc1::scene::Model{
                std::move(meshes),
                {{.mesh_index = 0},
                 {.parent_index = 0, .transform = {.translation = {-1, 0, 0}}, .mesh_index = 0},
                 {.transform = {.translation = {100, 0, 0}}, .mesh_index = 1}},
                {{.name = "Move",
                  .channels = {
                      {.times = {0, 1}, .values = std::vector<glm::vec3>{{0, 0, 0}, {2, 0, 0}}}}}}};
        }()};
    auto const &definition = model.definition();
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, vk::SampleCountFlagBits::e1};
    auto frame = renderer.make_frame_resources(static_cast<std::uint32_t>(definition.draw_count()));
    lc1::scene::FpsCamera camera;
    camera.set_position({0, 0, 4});
    camera.look_at({0, 0, 0});
    lc1::MaterialInfo material;
    material.parameters.base_color_factor = {0, 0, 0, 1};
    material.parameters.emissive_factor = {2, 0, 0};
    lc1::scene::Scene instance;
    lc1::scene::Scene other;
    auto *body = instance.add_model({.model = &model, .material = &material});
    auto *other_body = other.add_model({.model = &model, .material = &material});
    body->casts_shadow = false;
    body->depth_bias = true;
    auto const snapshot = instance.draws();
    check(!snapshot[0].casts_shadow && snapshot[0].depth_bias,
          "model draw extraction preserves shadow and depth bias flags");
    body->world_transform[3].x = 5;
    check(snapshot[0].model_matrix[3].x == 0 && instance.draws()[0].model_matrix[3].x == 5,
          "draw snapshots retain their submitted world transforms");
    body->visible = false;
    check(instance.draws().empty() && instance.draw_count() == 3,
          "hidden models retain their expanded draw capacity");
    body->visible = true;
    body->world_transform = glm::mat4{1};
    body->casts_shadow = true;
    body->depth_bias = false;
    auto render = [&](lc1::scene::Scene const &current) {
        auto const draws = current.draws();
        check(current.draw_count() == 3, "scene capacity counts repeated mesh nodes");
        check(draws.size() == 3 && draws[0].mesh == draws[1].mesh,
              "model nodes must share uploaded geometry while retaining independent transforms");
        check(model.meshes().size() == 2 && draws[0].mesh == &model.meshes()[0] &&
                  draws[2].mesh == &model.meshes()[1],
              "GPU model uploads every mesh and preserves definition mesh indices");
        return capture(device, renderer, frame, camera, current);
    };
    body->pose.play_animation(0, false);
    other_body->pose.play_animation(0, false);
    near(render(instance).center().r, 2, "animated node initially covers the center");
    instance.update_animations(1);
    near(render(instance).center().r, 0.24F, "animated hierarchy leaves the center uncovered");
    near(render(other).center().r, 2, "shared model instance keeps its own pose on the GPU");
    body->pose.play_animation(0, false);
    near(render(instance).center().r, 2, "restarting animation restores rendered geometry");
}

void player_models_test(lc1::Device const &device)
{
    lc1::Save const data{std::filesystem::path{__FILE__}.parent_path().parent_path() /
                         "runtime/saves/01"};
    auto const &character = data.characters().at(data.player(lc1::PlayerId{1})).model.value();
    auto const weapon_id = data.characters().at(data.player(lc1::PlayerId{1})).main_hand;
    auto const &weapon_config = data.weapons().at(weapon_id).model.value();
    lc1::scene::GpuModel barrel{
        device, lc1::load_configured_model(character, "runtime/assets", lc1::CharacterBody{})};
    lc1::scene::GpuModel sword{
        device, lc1::load_configured_model(weapon_config, "runtime/assets", lc1::CharacterBody{})};
    auto const wood = character.material.value().material();
    auto const metal = weapon_config.material.value().material();
    lc1::scene::Scene scene;
    auto *player = scene.add_model({.model = &barrel, .material = &wood});
    auto const grip = *barrel.definition().find_node(character.weapon_mount.value());
    auto *weapon = scene.add_model(
        {.model = &sword, .material = &metal, .world_transform = player->node_world_matrix(grip)});
    scene.add_light(lc1::scene::to_light(
        lc1::scene::DirectionalLight{.base = {.color = {1, 0.95F, 0.9F}, .intensity = 3},
                                     .direction = glm::normalize(glm::vec3{-1, -1, 1})}));
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, vk::SampleCountFlagBits::e1};
    auto frame = renderer.make_frame_resources(static_cast<std::uint32_t>(scene.draw_count()));
    lc1::scene::FpsCamera camera;
    camera.set_position({3, 2.5F, -4});
    camera.look_at({0.1F, 1, -0.3F});
    auto render = [&] {
        return capture(device, renderer, frame, camera, scene, {.width = 800, .height = 600});
    };
    auto const rest = render();
    weapon->pose.play_animation(0, false);
    scene.update_animations(0.28F);
    auto const swing = render();
    std::size_t changed = 0;
    for (std::size_t i = 0; i < rest.output.size(); i += 4)
        if (rest.output[i] != swing.output[i] || rest.output[i + 1] != swing.output[i + 1] ||
            rest.output[i + 2] != swing.output[i + 2])
            ++changed;
    check(changed > 100, "imported weapon swing must visibly change rendered coverage");
    std::filesystem::create_directories("artifacts");
    auto save = [](Capture const &result, char const *path) {
        std::ofstream image{path, std::ios::binary};
        image << "P6\n" << result.extent.width << ' ' << result.extent.height << "\n255\n";
        for (std::size_t i = 0; i < result.output.size(); i += 4)
            for (int channel = 0; channel < 3; ++channel)
                image.put(static_cast<char>(result.output[i + channel]));
        check(image.good(), "failed to save player model capture");
    };
    save(rest, "artifacts/player-weapon-rest.ppm");
    save(swing, "artifacts/player-weapon-swing.ppm");

    // Exercise production YAML -> world snapshot -> presentation-owned models.
    lc1::GameWorld initial{data, "runtime/dialogs/catalog.yaml"};
    auto snapshot = initial.snapshot();
    snapshot.session.players.push_back({73, 3});
    auto &material = snapshot.weapons.front().model.value().material.value();
    material.metallic_roughness_texture = material.base_color_texture.value();
    material.emissive_texture = material.base_color_texture.value();
    auto const slot = std::filesystem::path{"artifacts/player-models-save"};
    lc1::Save::write(slot, snapshot);
    lc1::GameWorld world{lc1::Save{slot}, "runtime/dialogs/catalog.yaml"};
    auto state = lc1::capture_world(world);
    {
        lc1::WorldPresentation presentation{device, "runtime/assets", world.continent(), state,
                                            {},     lc1::PlayerId{73}};
        auto const grass_capacity = presentation.object_capacity();
        auto const default_draws = presentation.scene().draws().size();
        presentation.set_grass_density(0);
        auto const bare_draws = presentation.scene().draws().size();
        presentation.set_grass_density(8);
        check(presentation.grass_density() == 8 && bare_draws < default_draws &&
                  default_draws < presentation.scene().draws().size(),
              "grass density changes visible grass batches");
        check(presentation.object_capacity() == grass_capacity,
              "grass density changes preserve frame resource capacity");
        presentation.set_grass_density(4);
        auto const observed_eye = glm::vec3{presentation.observer_eye_transform()[3]};
        auto const expected_eye = world.player(lc1::PlayerId{73}).eye_position();
        check(glm::distance(observed_eye, expected_eye) < 0.001F,
              "the observer camera mount belongs to the explicitly bound second player");
        auto const *equipped =
            presentation.weapon_model(world.player(lc1::PlayerId{1}).config().keyid);
        check(equipped, "presentation owns YAML-created weapon models");
        auto const &pbr = *equipped->material;
        check(pbr.base_color.texture && pbr.metallic_roughness.texture && pbr.emissive.texture,
              "YAML material loads external textures");
        check(pbr.base_color.texture->color_space() == lc1::TextureColorSpace::Srgb &&
                  pbr.metallic_roughness.texture->color_space() == lc1::TextureColorSpace::Linear,
              "texture cache distinguishes color and data uses of the same image");
        for (auto *bearer : world.characters())
            bearer->set_body_yaw(static_cast<float>(bearer->config().keyid) * 30);
        lc1::CharacterIntent swing_intent;
        swing_intent.swing_weapon = true;
        for (auto *bearer : world.characters()) {
            apply_character_intent(*bearer, 0, world.continent(), swing_intent);
            auto const *weapon = bearer->config().main_hand;
            if (!weapon)
                continue;
            state = lc1::capture_world(world);
            presentation.update(0, {});
            auto const &pose = presentation.weapon_model(bearer->config().keyid)->pose;
            check(pose.playback() && pose.playback()->time == 0,
                  "accepted attack snapshot starts player and NPC weapon animations");
            presentation.update(0.1F, {});
            apply_character_intent(*bearer, 0, world.continent(), {});
            state = lc1::capture_world(world);
            presentation.update(0, {});
            check(pose.playback()->time > 0.09F,
                  "an empty intent does not restart the weapon animation");
            apply_character_intent(*bearer, 0, world.continent(), swing_intent);
            state = lc1::capture_world(world);
            presentation.update(0, {});
            check(pose.playback()->time > 0.09F,
                  "continuous swing requests cannot restart an attack during cooldown");
            apply_character_intent(*bearer, 2.0F, world.continent(), {});
            apply_character_intent(*bearer, 0, world.continent(), swing_intent);
            state = lc1::capture_world(world);
            presentation.update(0, {});
            check(pose.playback()->time == 0,
                  "cooldown expires even without requests and allows the next swing");
            lc1::AiCharacterController ai{
                world.dialogs(),
                [&world](lc1::CharacterId id) -> lc1::Character & { return world.character(id); },
                std::array{bearer}, std::vector<lc1::Waypoint>{{.position = bearer->position()}}};
            ai.possess(*bearer);
            for (int frame = 0; frame < 10; ++frame) {
                auto const request = ai.decide(0.02F);
                check(request.swing_weapon, "nearby AI keeps requesting swings while stationary");
                apply_character_intent(*bearer, 0.02F, world.continent(), request);
                state = lc1::capture_world(world);
                presentation.update(0.02F, {});
            }
            check(pose.playback()->time > 0.19F,
                  "AI requests let the weapon advance through the visible part of the swing");
            apply_character_intent(*bearer, 2.0F, world.continent(), swing_intent);
        }
        apply_character_intent(world.player(lc1::PlayerId{1}), 0, world.continent(), {.nod = true});
        state = lc1::capture_world(world);
        presentation.update(0.28F, {});
        auto const &remote_nod =
            presentation.character_model(world.player(lc1::PlayerId{1}).config().keyid).pose;
        check(remote_nod.playback() && remote_nod.playback()->time > 0.27F,
              "another player's nod sequence starts and advances its character animation");
        std::vector<lc1::scene::SceneModel const *> weapon_models;
        for (auto const *bearer : world.characters()) {
            auto const *weapon = bearer->config().main_hand;
            if (!weapon)
                continue;
            auto const *model = presentation.weapon_model(bearer->config().keyid);
            check(model && !std::ranges::contains(weapon_models, model),
                  "each player's or NPC's equipped weapon has its own scene instance");
            check(model->pose.playback() && model->pose.playback()->time > 0.27F,
                  "player and NPC weapon swings advance through character intent");
            weapon_models.push_back(model);
            auto const mount = *presentation.character_model(bearer->config().keyid)
                                    .model->definition()
                                    .find_node(bearer->config().model.weapon_mount.value());
            auto const expected =
                presentation.character_model(bearer->config().keyid).node_world_matrix(mount);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    near(model->world_transform[column][row], expected[column][row],
                         "every weapon follows its bearer's current mount");
        }
        check(weapon_models.size() == 3, "default player and both armed NPCs display weapons");
        check(weapon_models[1]->model == weapon_models[2]->model,
              "NPC weapons share GPU resources while retaining independent scene instances");
        check(equipped->pose.playback()->time > 0.27F, "configured action animation advances");
        auto configured_frame = renderer.make_frame_resources(presentation.object_capacity());
        for (auto const *bearer : world.characters()) {
            if (bearer == &world.player(lc1::PlayerId{1}))
                continue;
            auto const *weapon = bearer->config().main_hand;
            if (!weapon)
                continue;
            lc1::scene::Scene isolated;
            isolated.add_model(presentation.character_model(bearer->config().keyid));
            isolated.add_light(lc1::scene::to_light(
                lc1::scene::DirectionalLight{.base = {.color = {1, 0.95F, 0.9F}, .intensity = 3},
                                             .direction = glm::normalize(glm::vec3{-1, -1, 1})}));
            camera.set_position(bearer->position() + glm::vec3{3, 2.5F, -4});
            camera.look_at(bearer->position() + glm::vec3{0.1F, 1, -0.3F});
            auto const unarmed = capture(device, renderer, configured_frame, camera, isolated,
                                         {.width = 800, .height = 600});
            isolated.add_model(*presentation.weapon_model(bearer->config().keyid));
            auto const armed = capture(device, renderer, configured_frame, camera, isolated,
                                       {.width = 800, .height = 600});
            std::size_t changed = 0;
            for (std::size_t pixel = 0; pixel < armed.output.size(); pixel += 4)
                if (armed.output[pixel] != unarmed.output[pixel] ||
                    armed.output[pixel + 1] != unarmed.output[pixel + 1] ||
                    armed.output[pixel + 2] != unarmed.output[pixel + 2])
                    ++changed;
            check(changed > 100, "each NPC weapon visibly changes its rendered image");
            save(armed,
                 std::format("artifacts/character-{}-weapon.ppm", bearer->config().keyid).c_str());
        }
        auto &victim = world.character(lc1::CharacterId{2});
        victim.set_controller(nullptr);
        victim.update(0.7F, world.continent());
        state = lc1::capture_world(world);
        presentation.update(0, {});
        camera.set_position(victim.position() + glm::vec3{3, 2.5F, -4});
        camera.look_at(victim.position() + glm::vec3{0, 0.8F, -0.4F});
        auto capture_victim = [&] {
            lc1::scene::Scene isolated;
            isolated.add_model(presentation.character_model(victim.id().value));
            if (auto const *weapon = presentation.weapon_model(victim.id().value))
                isolated.add_model(*weapon);
            isolated.add_light(lc1::scene::to_light(
                lc1::scene::DirectionalLight{.base = {.color = {1, 0.95F, 0.9F}, .intensity = 3},
                                             .direction = glm::normalize(glm::vec3{-1, -1, 1})}));
            return capture(device, renderer, configured_frame, camera, isolated,
                           {.width = 800, .height = 600});
        };
        auto const standing = capture_victim();
        save(standing, "artifacts/combat-standing.ppm");
        victim.take_damage(10);
        victim.update(0.15F, world.continent());
        state = lc1::capture_world(world);
        presentation.update(0, {});
        auto const recoil = capture_victim();
        save(recoil, "artifacts/combat-hit.ppm");
        std::size_t reaction_pixels = 0;
        for (std::size_t pixel = 0; pixel < recoil.output.size(); pixel += 4)
            if (recoil.output[pixel] != standing.output[pixel] ||
                recoil.output[pixel + 1] != standing.output[pixel + 1] ||
                recoil.output[pixel + 2] != standing.output[pixel + 2])
                ++reaction_pixels;
        check(reaction_pixels > 100, "production hit state visibly animates the character");
        victim.take_damage(victim.config().max_health());
        victim.update(0.3F, world.continent());
        state = lc1::capture_world(world);
        presentation.update(0, {});
        save(capture_victim(), "artifacts/combat-falling.ppm");
        victim.update(0.3F, world.continent());
        state = lc1::capture_world(world);
        presentation.update(0, {});
        auto const corpse = capture_victim();
        save(corpse, "artifacts/combat-dead.ppm");
        reaction_pixels = 0;
        for (std::size_t pixel = 0; pixel < corpse.output.size(); pixel += 4)
            if (corpse.output[pixel] != standing.output[pixel] ||
                corpse.output[pixel + 1] != standing.output[pixel + 1] ||
                corpse.output[pixel + 2] != standing.output[pixel + 2])
                ++reaction_pixels;
        check(reaction_pixels > 100, "production death state visibly leaves a fallen character");
        auto const fallen = presentation.character_model(victim.id().value).world_transform;
        presentation.update(1, {});
        check(fallen == presentation.character_model(victim.id().value).world_transform &&
                  !presentation.character_model(victim.id().value).pose.playback(),
              "death holds the fallen pose and stops idle and nod animations");
        camera.set_position(world.player(lc1::PlayerId{1}).position() + glm::vec3{3, 2.5F, -4});
        camera.look_at(world.player(lc1::PlayerId{1}).position() + glm::vec3{0.1F, 1, -0.3F});
        capture(device, renderer, configured_frame, camera, presentation.scene(),
                {.width = 800, .height = 600});
        device.wait_idle();
    }
    std::filesystem::remove(slot / "checkpoint.yaml");
    std::filesystem::remove(slot);
}

void showcase_test(lc1::Device const &device)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Srgb}, vk::SampleCountFlagBits::e4};
    auto const blocks = lc1::demo::PbrShowcase::spawn_blocks();
    auto const continent = lc1::Continent::make_prototype(blocks);
    for (int z = -25; z <= 25; z += 2)
        check(lc1::WorldCollision{continent}.can_stand(continent.spawn() + glm::vec3{0, 0, z}, 0.3F,
                                                       1.8F),
              "showcase obstructs the main road");
    check(!lc1::WorldCollision{continent}.can_stand(continent.spawn() + glm::vec3{-13, 0, 15}, 0.3F,
                                                    1.8F),
          "showcase pedestal has no static collision");
    std::array<std::uint8_t, 4> const pixel{255, 255, 255, 255};
    lc1::GpuTexture white{device, {.width = 1, .height = 1}, pixel};
    lc1::ContinentView terrain{device, white, continent};
    lc1::demo::PbrShowcase scene{device, continent.spawn()};
    // Panel lettering is world text; the same view is captured with and without it.
    lc1::GpuFont font{device,
                      lc1::text::GlyphAtlas{"runtime/assets/fonts/LiberationSans-Regular.ttf"}};
    lc1::TextFrameInput const text{
        .font = &font, .world = {scene.labels().begin(), scene.labels().end()}, .screen = {}};
    std::vector<lc1::scene::SceneModel> draws{terrain.draw_items().begin(),
                                              terrain.draw_items().end()};
    auto const offset = draws.size();
    draws.insert(draws.end(), scene.draws().begin(), scene.draws().end());
    auto frame = renderer.make_frame_resources(static_cast<std::uint32_t>(draws.size()));
    lc1::scene::FpsCamera camera;
    camera.set_zfar(800);
    lc1::MapCameraController camera_controller{continent.spawn()};
    camera_controller.apply(camera);
    std::vector lights{lc1::scene::to_light(
        lc1::scene::DirectionalLight{.base = {.color = {1, 0.95F, 0.9F}, .intensity = 0.3F},
                                     .direction = glm::normalize(glm::vec3{-1, -1, -1})})};
    lights.insert(lights.end(), scene.lights().begin(), scene.lights().end());
    auto save = [&](Capture const &result, char const *path, float exposure = 1.0F) {
        check_tone_map(result, exposure, true);
        std::ofstream image{path, std::ios::binary};
        image << "P6\n" << result.extent.width << ' ' << result.extent.height << "\n255\n";
        for (std::size_t i = 0; i < result.output.size(); i += 4)
            for (int c = 0; c < 3; ++c)
                image.put(static_cast<char>(result.output[i + c]));
        check(image.good(), "failed to save showcase capture");
    };
    std::filesystem::create_directories("artifacts");
    lc1::scene::Scene gi_scene;
    std::vector<lc1::scene::SceneModel *> gi_models;
    for (auto const &draw : draws)
        gi_models.push_back(gi_scene.add_model(draw));
    for (auto const &light : lights)
        gi_scene.add_light(light);
    auto timed_scene = [&](double &elapsed) {
        return capture(device, renderer, frame, camera, gi_scene, {.width = 800, .height = 600}, {},
                       vk::Format::eR8G8B8A8Srgb, {}, &elapsed);
    };
    double elapsed = 0;
    for (int i = 0; i < 4; ++i)
        timed_scene(elapsed);
    double direct_ms = 0;
    for (int i = 0; i < 8; ++i) {
        timed_scene(elapsed);
        direct_ms += elapsed / 8;
    }
    save(timed_scene(elapsed), "artifacts/ddgi-disabled.ppm");
    gi_scene.set_diffuse_gi(lc1::scene::DiffuseGi{.center = continent.spawn() + glm::vec3{0, 6, 0},
                                                  .sky_radiance = {0.08F, 0.105F, 0.14F}});
    timed_scene(elapsed);
    auto const initial_ms = elapsed;
    for (int i = 0; i < 16; ++i)
        timed_scene(elapsed);
    double gi_ms = 0;
    for (int i = 0; i < 8; ++i) {
        timed_scene(elapsed);
        gi_ms += elapsed / 8;
    }
    save(timed_scene(elapsed), "artifacts/ddgi-enabled.ppm");
    save(capture(device, renderer, frame, camera, gi_scene, {.width = 800, .height = 600},
                 {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb),
         "artifacts/ddgi-daylight.ppm", 2.0F);
    auto const overview_camera = camera;
    camera.set_position(continent.spawn() + glm::vec3{-6, 1.65F, 4});
    camera.look_at(continent.spawn() + glm::vec3{-6, 0, 12});
    save(capture(device, renderer, frame, camera, gi_scene, {.width = 1200, .height = 800},
                 {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb),
         "artifacts/pbr-ground.ppm", 2.0F);
    camera.set_position({-72, 1.2F, 110});
    camera.look_at({-82, 0.12F, 107.5F});
    save(capture(device, renderer, frame, camera, gi_scene, {.width = 1200, .height = 800},
                 {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb),
         "artifacts/pbr-roadside.ppm", 2.0F);
    for (auto const &batch : terrain.grass_batches())
        gi_models[batch.draw_index]->visible = false;
    auto const bare_road =
        capture(device, renderer, frame, camera, gi_scene, {.width = 1200, .height = 800},
                {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb);
    save(bare_road, "artifacts/pbr-grass-zero.ppm", 2.0F);
    for (auto const &batch : terrain.grass_batches())
        gi_models[batch.draw_index]->visible = true;
    auto const grassy_road =
        capture(device, renderer, frame, camera, gi_scene, {.width = 1200, .height = 800},
                {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb);
    save(grassy_road, "artifacts/pbr-grass-full.ppm", 2.0F);
    check(bare_road.output != grassy_road.output, "grass density must change the rendered scene");
    for (auto const &batch : terrain.grass_batches())
        gi_models[batch.draw_index]->visible = batch.level <= 4;

    camera.set_position({-93, 0.65F, 108});
    camera.look_at({-95.4F, 0.1F, 105.5F});
    save(capture(device, renderer, frame, camera, gi_scene, {.width = 1200, .height = 800},
                 {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb),
         "artifacts/pbr-roadside-detail.ppm", 2.0F);
    camera = overview_camera;
    gi_scene.set_diffuse_gi(lc1::scene::DiffuseGi{.center = continent.spawn() + glm::vec3{0, 6, 0},
                                                  .emissive_lighting = true});
    for (int i = 0; i < 128; ++i)
        timed_scene(elapsed);
    save(timed_scene(elapsed), "artifacts/ddgi-zero-sky.ppm");
    lc1::scene::Scene led_scene;
    for (auto const &draw : draws)
        led_scene.add_model(draw);
    // Explicit disable resets the shared renderer's previous daylight history.
    capture(device, renderer, frame, camera, led_scene, {.width = 800, .height = 600});
    led_scene.set_diffuse_gi(lc1::scene::DiffuseGi{.center = continent.spawn() + glm::vec3{0, 6, 0},
                                                   .emissive_lighting = true});
    for (int i = 0; i < 32; ++i)
        capture(device, renderer, frame, camera, led_scene, {.width = 800, .height = 600});
    ddgi_candidate_tests(device, frame);
    auto previous_led =
        capture(device, renderer, frame, camera, led_scene, {.width = 800, .height = 600});
    float maximum_led_delta = 0;
    for (int i = 0; i < 32; ++i) {
        auto current_led =
            capture(device, renderer, frame, camera, led_scene, {.width = 800, .height = 600});
        for (std::size_t pixel = 0; pixel < current_led.hdr.size(); ++pixel)
            for (int c = 0; c < 3; ++c)
                maximum_led_delta =
                    std::max(maximum_led_delta,
                             std::abs(current_led.hdr[pixel][c] - previous_led.hdr[pixel][c]));
        previous_led = std::move(current_led);
    }
    std::cout << "Static LED showcase maximum frame delta: " << maximum_led_delta << std::endl;
    check(maximum_led_delta < 0.005F, "static LED showcase must not flash across probe updates");
    save(capture(device, renderer, frame, camera, led_scene, {.width = 800, .height = 600},
                 {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb),
         "artifacts/ddgi-led-only.ppm", 2.0F);
    camera.set_position(continent.spawn() + glm::vec3{-13, 3.3F, 28});
    camera.look_at(continent.spawn() + glm::vec3{-13, 1.2F, 15});
    save(capture(device, renderer, frame, camera, led_scene, {.width = 800, .height = 600},
                 {.exposure = 2.0F}, vk::Format::eR8G8B8A8Srgb),
         "artifacts/ddgi-led-near.ppm", 2.0F);
    camera = overview_camera;
    std::cout << "GPU scene 800x600, " << vk::to_string(renderer.sample_count()) << ": direct "
              << direct_ms << " ms, DDGI steady " << gi_ms << " ms, DDGI initialization "
              << initial_ms << " ms\n";
    for (int i = 0; i < 8; ++i)
        capture(device, renderer, frame, camera, lights, draws, {.width = 1200, .height = 800}, {},
                vk::Format::eR8G8B8A8Srgb);
    auto const overview =
        capture(device, renderer, frame, camera, lights, draws, {.width = 1200, .height = 800}, {},
                vk::Format::eR8G8B8A8Srgb, text);
    check(std::ranges::any_of(overview.hdr, [](auto p) { return p.r > 1; }),
          "showcase has no HDR emission");
    auto const unlettered = capture(device, renderer, frame, camera, lights, draws,
                                    {.width = 1200, .height = 800}, {}, vk::Format::eR8G8B8A8Srgb);
    check(overview.output != unlettered.output, "panel lettering is missing from the scene");
    save(overview, "artifacts/pbr-showcase.ppm");
    check(frame.ray_query.built_instances.size() ==
              static_cast<std::size_t>(std::ranges::count_if(
                  draws, [](auto const &draw) { return draw.visible && draw.casts_shadow; })),
          "printed exhibit labels must not cast duplicate shadows");
    camera.set_position(continent.spawn() + glm::vec3{13, 1.8F, 33});
    camera.look_at(continent.spawn() + glm::vec3{13, 2.9F, 15});
    save(capture(device, renderer, frame, camera, lights, draws, {.width = 1000, .height = 750}, {},
                 vk::Format::eR8G8B8A8Srgb),
         "artifacts/pbr-transforms.ppm");
    camera.set_position(continent.spawn() + glm::vec3{-13, 8, 4});
    camera.look_at(continent.spawn() + glm::vec3{-13, 1, -9});
    save(capture(device, renderer, frame, camera, lights, draws, {.width = 1000, .height = 600}, {},
                 vk::Format::eR8G8B8A8Srgb),
         "artifacts/pbr-normals.ppm");
    // Check a ground-level view and actual emission-only rendering in the same world.
    camera.set_position(continent.spawn() + glm::vec3{-13, 3.3F, 34});
    camera.look_at(continent.spawn() + glm::vec3{-13, 2.5F, 15});
    save(capture(device, renderer, frame, camera, {}, draws, {.width = 800, .height = 500}, {},
                 vk::Format::eR8G8B8A8Srgb),
         "artifacts/pbr-emission.ppm");
    scene.update(1.0F);
    std::copy(scene.draws().begin(), scene.draws().end(), draws.begin() + offset);
    capture(device, renderer, frame, camera, lights, draws, {.width = 127, .height = 81}, {},
            vk::Format::eR8G8B8A8Srgb);
}

} // namespace

int main(int argc, char **argv)
{
    try {
        bool const animation_only = argc == 2 && std::string_view{argv[1]} == "--model-animation";
        bool const player_only = argc == 2 && std::string_view{argv[1]} == "--player-models";
        bool const showcase_only = argc == 2 && std::string_view{argv[1]} == "--showcase";
        check(argc == 1 || animation_only || player_only || showcase_only,
              "usage: lc1_pbr_tests [--model-animation|--player-models|--showcase]");
        lc1::VulkanLoader loader;
        lc1::Window window{64, 64, "PBR GPU tests"};
        glfwHideWindow(window.handle());
        auto extensions = window.required_instance_extensions();
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        lc1::Instance instance{loader, std::move(extensions), {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        lc1::Surface surface{instance, window};
        auto device = lc1::test::make_render_device(instance, *surface.raii());
        if (player_only) {
            player_models_test(device);
            device.wait_idle();
            std::cout << "Imported player and weapon swing GPU tests passed\n";
            return 0;
        }
        if (animation_only) {
            model_animation_test(device);
            device.wait_idle();
            std::cout << "Model node animation GPU tests passed\n";
            return 0;
        }
        if (showcase_only) {
            showcase_test(device);
            device.wait_idle();
            std::cout << "Terrain, grass density and showcase GPU tests passed\n";
            return 0;
        }
        ddgi_tests(device);
        mutable_material_tests(device);
        material_tests(device, vk::SampleCountFlagBits::e1);
        material_tests(device, vk::SampleCountFlagBits::e4);
        normal_map_tests(device, vk::SampleCountFlagBits::e1);
        normal_map_tests(device, vk::SampleCountFlagBits::e4);
        model_animation_test(device);
        showcase_test(device);
        device.wait_idle();
        std::cout << "PBR factors, BRDF, textures, HDR, tone mapping, MSAA, resize, node animation "
                     "and mutable materials "
                     "lifetime passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
