#include "render-device.hpp"

#include "lc1/image.hpp"

#include "lc1/platform/window.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/gpu-model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/model-instance.hpp"
#include "lc1/scene/scene.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/presentation/surface.hpp"
#include "lc1/vk/render/render-graph.hpp"
#include "lc1/vk/render/renderer.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, std::string const &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

constexpr vk::Extent2D test_extent{64, 64};

struct Capture {
    vk::Extent2D extent;
    std::vector<glm::vec4> hdr;
    std::vector<std::uint8_t> ldr;
};

// Renders one frame and reads back the composited HDR image plus the LDR output.
// The HDR readback keeps the relational checks independent of tone mapping.
// `composited` selects the frame's post-composite HDR image; with the cloud pass
// disabled that image is never written, and the plain HDR resolve is the frame's
// colour instead.
Capture capture(lc1::Device const &device, lc1::Renderer &renderer, lc1::FrameResources &frame,
                lc1::scene::FpsCamera &camera, lc1::scene::Scene const &scene,
                bool composited = true, vk::Extent2D extent = test_extent,
                double *frame_ms = nullptr, lc1::TextFrameInput text = {})
{
    std::optional<vk::raii::QueryPool> timing;
    if (frame_ms)
        timing.emplace(device.raii().createQueryPool(
            {.queryType = vk::QueryType::eTimestamp, .queryCount = 2}));
    lc1::GpuImage output{device,
                         vk::Format::eR8G8B8A8Unorm,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    auto const ldr_bytes = vk::DeviceSize{extent.width} * extent.height * 4;
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
        renderer.record(wrapped, frame, {&output.view(), extent}, scene, camera, {},
                        std::move(text));
        if (timing)
            commands.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, **timing, 1);
        output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eTransferSrcOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eCopy,
                                 vk::AccessFlagBits2::eTransferRead);
        lc1::GpuImage const &hdr_image = composited ? *frame.hdr_cloud_image : *frame.hdr_image;
        hdr_image.transition_layout(
            commands, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1}};
        commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                                   region);
        region.bufferOffset = hdr_offset;
        commands.copyImageToBuffer(*hdr_image.raii(), vk::ImageLayout::eTransferSrcOptimal,
                                   *readback, region);
        hdr_image.transition_layout(
            commands, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        vk::MemoryBarrier2 const host_barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                              .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                              .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                              .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(host_barrier));
    });
    Capture result;
    result.extent = extent;
    result.ldr.resize(ldr_bytes);
    readback.getAllocation().copyToMemory(0, result.ldr.data(), ldr_bytes);
    if (timing) {
        std::array<std::uint64_t, 2> stamps{};
        check(timing->getResults(0, 2, sizeof(stamps), stamps.data(), sizeof(std::uint64_t),
                                 vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait) ==
                  vk::Result::eSuccess,
              "GPU timestamps available after submission completion");
        *frame_ms = static_cast<double>(stamps[1] - stamps[0]) *
                    device.raii_physical().getProperties().limits.timestampPeriod / 1e6;
    }
    std::vector<std::uint16_t> half(static_cast<std::size_t>(extent.width) * extent.height * 4);
    readback.getAllocation().copyToMemory(hdr_offset, half.data(),
                                          half.size() * sizeof(std::uint16_t));
    result.hdr.reserve(half.size() / 4);
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

void save(Capture const &capture, char const *path)
{
    std::ofstream file{path, std::ios::binary};
    file << "P6\n" << capture.extent.width << ' ' << capture.extent.height << "\n255\n";
    for (std::size_t pixel = 0; pixel < capture.ldr.size(); pixel += 4)
        for (int channel = 0; channel < 3; ++channel)
            file.put(static_cast<char>(capture.ldr[pixel + channel]));
    check(file.good(), "cloud capture saved");
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

float mean_radiance(Capture const &capture, std::vector<bool> const &mask)
{
    double total = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < capture.hdr.size(); ++i) {
        if (!mask[i])
            continue;
        total += glm::dot(glm::vec3{capture.hdr[i]}, glm::vec3{0.2126F, 0.7152F, 0.0722F});
        ++count;
    }
    return count == 0 ? 0.0F : static_cast<float>(total / static_cast<double>(count));
}

void exercise(lc1::Device const &device, vk::SampleCountFlagBits samples)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Unorm}, samples};
    auto &clouds = renderer.cloud_settings();
    check(!clouds.enabled, "volumetric clouds must be opt-in");

    // A ground plane below the horizon: rays that hit it must never march the layer.
    lc1::GpuTexture texture{
        device, lc1::Image::load_from_file("runtime/assets/textures/prototype-white.png")};
    auto material =
        lc1::MaterialInfo{.parameters = {.metallic_factor = 0.0F, .roughness_factor = 0.8F},
                          .base_color = {.texture = &texture}};
    std::array const vertices{
        lc1::Vertex{.position = {-400, 0, -400}, .uv = {0, 0}},
        lc1::Vertex{.position = {-400, 0, 400}, .uv = {0, 1}},
        lc1::Vertex{.position = {400, 0, 400}, .uv = {1, 1}},
        lc1::Vertex{.position = {400, 0, -400}, .uv = {1, 0}},
    };
    std::array<std::uint32_t, 6> const indices{0, 1, 2, 0, 2, 3};
    lc1::scene::GpuModel ground{lc1::GpuMesh{device, std::span<lc1::Vertex const>{vertices},
                                             std::span<std::uint32_t const>{indices}}};
    lc1::scene::Scene scene;
    scene.add_model(
        lc1::scene::SceneModel{.model = &ground, .material = &material, .depth_bias = false});
    // Sun in front of the camera and well above the horizon: the layer must show
    // directional lighting, forward scattering and self shadowing.
    scene.add_light(lc1::scene::to_light(
        lc1::scene::DirectionalLight{.base = {.color = {1.0F, 0.96F, 0.9F}, .intensity = 0.45F},
                                     .direction = {0.4F, -0.72F, 0.57F}}));
    // No diffuse GI: its probes accumulate across frames, and these checks compare
    // successive frames of the same scene exactly.
    // A daylight sky, as the game drives it: the layer's ambient term comes from here.
    scene.set_sky(lc1::scene::Sky{.color = {0.22F, 0.34F, 0.55F}});

    lc1::scene::FpsCamera camera;
    camera.set_zfar(600.0F);
    camera.set_position({0.0F, 40.0F, 0.0F});
    // Level view: the horizon splits the frame, so the upper rows look into the layer
    // while the lower rows meet the ground plane well in front of it.
    auto frame = renderer.make_frame_resources(1);
    auto const clear_sky = capture(device, renderer, frame, camera, scene, false);

    clouds.enabled = true;
    auto const first = capture(device, renderer, frame, camera, scene);
    check(frame.hdr_cloud_image.has_value(), "cloud rendering must produce a composited HDR image");
    check(frame.cloud_image.has_value(),
          "cloud rendering must produce a radiance/transmittance image");

    // Which pixels the layer touched, relative to the cloudless frame.
    std::vector<bool> touched(first.hdr.size(), false);
    std::size_t sky_total = 0;
    std::size_t touched_sky = 0;
    for (std::size_t i = 0; i < touched.size(); ++i) {
        auto const delta = glm::abs(glm::vec3{first.hdr[i] - clear_sky.hdr[i]});
        touched[i] = delta.x > 1e-3F || delta.y > 1e-3F || delta.z > 1e-3F;
        if (i / test_extent.width < test_extent.height / 2) {
            ++sky_total;
            touched_sky += touched[i] ? 1 : 0;
        }
    }
    check(touched_sky > sky_total / 10,
          "the layer must cover a meaningful part of the upper, sky-facing rows");

    // Depth limits the march: the ground is in front of the layer, so those pixels
    // must come through unchanged instead of being fogged over.
    std::size_t ground_pixels = 0;
    for (std::size_t i = 0; i < touched.size(); ++i) {
        // The lowest rows look at the ground plane; nothing of the layer is closer.
        if (i / test_extent.width < test_extent.height * 3 / 4)
            continue;
        auto const delta = glm::abs(glm::vec3{first.hdr[i] - clear_sky.hdr[i]});
        check(delta.x + delta.y + delta.z < 1e-3F,
              "pixels in front of the layer must keep the unclouded scene");
        ++ground_pixels;
    }
    check(ground_pixels > 0, "the depth-occluded region must be non-empty");

    // Structure, not a flat tint: neighbouring sky pixels differ.
    float const sky_mean = mean_radiance(first, touched);
    double variance = 0.0;
    std::size_t sky_count = 0;
    for (std::size_t i = 0; i < touched.size(); ++i) {
        if (!touched[i])
            continue;
        float const luminance =
            glm::dot(glm::vec3{first.hdr[i]}, glm::vec3{0.2126F, 0.7152F, 0.0722F});
        variance += (luminance - sky_mean) * (luminance - sky_mean);
        ++sky_count;
    }
    variance /= std::max<std::size_t>(sky_count, 1);
    check(std::sqrt(variance) > 0.01F, "cloud coverage must have visible structure");

    // Zero density is the neutral element: the composite must reproduce the scene.
    clouds.density = 0.0F;
    auto const no_density = capture(device, renderer, frame, camera, scene);
    for (std::size_t i = 0; i < no_density.hdr.size(); ++i)
        check(no_density.hdr[i] == clear_sky.hdr[i],
              "a layer without density must not change the scene");
    clouds.density = 1.0F;

    // Coverage changes how much of the sky the layer occupies.
    clouds.debug = lc1::CloudSettings::Debug::ViewTransmittance;
    clouds.coverage = 0.15F;
    auto const sparse = capture(device, renderer, frame, camera, scene);
    clouds.coverage = 0.95F;
    auto const overcast = capture(device, renderer, frame, camera, scene);
    std::vector<bool> all(first.hdr.size(), true);
    float const sparse_opacity = mean_radiance(sparse, all);
    float const overcast_opacity = mean_radiance(overcast, all);
    check(overcast_opacity > sparse_opacity + 0.05F,
          "higher coverage must make the layer optically thicker");
    clouds.coverage = 0.5F;

    // The debug views are what a user reads to understand the layer.
    clouds.debug = lc1::CloudSettings::Debug::Density;
    auto const density_view = capture(device, renderer, frame, camera, scene);
    check(mean_radiance(density_view, all) > 0.01F, "the density view must show the cloud body");
    clouds.debug = lc1::CloudSettings::Debug::SunTransmittance;
    auto const sun_view = capture(device, renderer, frame, camera, scene);
    float const sun_mean = mean_radiance(sun_view, all);
    check(sun_mean > 0.05F && sun_mean < 1.0F, "sun transmittance must be partial under the sun");
    clouds.debug = lc1::CloudSettings::Debug::StepCount;
    auto const steps_view = capture(device, renderer, frame, camera, scene);
    check(mean_radiance(steps_view, all) > 0.0F, "the step count view must report marched steps");
    clouds.debug = lc1::CloudSettings::Debug::Off;

    // Disabling the pass returns the exact cloudless image: no stale images, no
    // dangling graph input.
    clouds.enabled = false;
    auto const restored = capture(device, renderer, frame, camera, scene, false);
    for (std::size_t i = 0; i < restored.hdr.size(); ++i)
        check(restored.hdr[i] == clear_sky.hdr[i],
              "disabling the cloud pass must restore the cloudless frame");

    // Readable captures for the layer itself, its absence and its density field. These
    // are review artifacts, not assertions: the behavioural checks are above. They come
    // from the multisampled run when the device has one, matching the game's path.
    if (samples != vk::SampleCountFlagBits::e1) {
        std::filesystem::create_directories("artifacts");
        constexpr vk::Extent2D artifact_extent{480, 270};
        double clear_ms = 0.0;
        double cloud_ms = 0.0;
        // From below the layer but well off the ground: a view into the deck rather
        // than along it, where the layer's thickness and structure are visible.
        auto showcase_camera = camera;
        showcase_camera.set_position({0.0F, 800.0F, 0.0F});
        showcase_camera.add_pitch(glm::radians(20.0F));
        clouds.enabled = true;
        // A character-name style world label over the layer: the outline has to keep it
        // readable over sky, cloud and terrain alike.
        lc1::GpuFont const font{
            device, lc1::text::GlyphAtlas{"runtime/assets/fonts/SourceHanSansCN-Regular.otf", {}}};
        // Inside the camera's far plane: a label beyond it is clipped, not dimmed.
        auto const label_center =
            showcase_camera.position() + showcase_camera.forward() * 260.0F + glm::vec3{0, 45, 0};
        lc1::TextFrameInput const text{
            .font = &font,
            .world = {{.text = "lc1 from Empire",
                       .center = label_center,
                       .line_height = 40.0F,
                       .normal = showcase_camera.position() - label_center}},
            .screen = {},
        };
        save(capture(device, renderer, frame, showcase_camera, scene, true, artifact_extent,
                     &cloud_ms, text),
             "artifacts/clouds.ppm");
        std::cout << std::format("480x270 frame: {:.3f} ms without clouds, {:.3f} ms with them\n",
                                 clear_ms, cloud_ms);
        double full_clear = 0.0;
        double full_cloud = 0.0;
        constexpr vk::Extent2D full_extent{1244, 1516};
        clouds.enabled = false;
        capture(device, renderer, frame, showcase_camera, scene, false, full_extent, &full_clear);
        clouds.enabled = true;
        capture(device, renderer, frame, showcase_camera, scene, true, full_extent, &full_cloud);
        std::cout << std::format("1244x1516 frame: {:.3f} ms without clouds, {:.3f} ms with them\n",
                                 full_clear, full_cloud);
        clouds.enabled = false;
        save(capture(device, renderer, frame, showcase_camera, scene, false, artifact_extent,
                     &clear_ms, text),
             "artifacts/clouds-clear.ppm");
        clouds.enabled = true;
        clouds.debug = lc1::CloudSettings::Debug::Density;
        save(capture(device, renderer, frame, showcase_camera, scene, true, artifact_extent),
             "artifacts/clouds-density.ppm");
        clouds.debug = lc1::CloudSettings::Debug::Off;
    }

    // Settings are validated once, at registration.
    // Registration alone validates: the rejected frames below never record or execute,
    // and the target is only read for its extent.
    lc1::RenderGraph graph;
    lc1::RenderTarget const target{nullptr, test_extent};
    clouds.enabled = true;
    clouds.top = clouds.bottom;
    rejects([&] { renderer.add_passes(graph, frame, target, scene, camera); },
            "a layer with no thickness must be rejected");
    clouds.top = 3500.0F;
    clouds.view_steps = 0;
    rejects([&] { renderer.add_passes(graph, frame, target, scene, camera); },
            "a ray march without view steps must be rejected");
    clouds.view_steps = 48;
    clouds.coverage = 0.0F;
    rejects([&] { renderer.add_passes(graph, frame, target, scene, camera); },
            "zero coverage must be rejected rather than dividing by it");
    clouds.coverage = 0.5F;
    device.wait_idle();
}

} // namespace

int main()
{
    try {
        lc1::VulkanLoader loader;
        lc1::Window window{64, 64, "volumetric cloud GPU tests"};
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
        std::cout << "volumetric cloud layer, depth occlusion, coverage, debug views and "
                     "enable/disable passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
