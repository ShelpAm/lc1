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
                double *frame_ms = nullptr, lc1::TextFrameInput text = {},
                lc1::OutputSettings output_settings = {})
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
        renderer.record(wrapped, frame, {&output.view(), extent}, scene, camera, output_settings,
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
    // The behavioural checks need several cloud masses in frame whichever way the
    // shipped default scale is tuned, so they pin their own.
    clouds.noise_scale = 8000.0F;
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

    // Zero density preserves geometry; sky still includes directional atmosphere and sun.
    clouds.density = 0.0F;
    auto const no_density = capture(device, renderer, frame, camera, scene);
    for (std::size_t i = 0; i < no_density.hdr.size(); ++i)
        if (i / test_extent.width >= test_extent.height * 3 / 4)
            check(no_density.hdr[i] == clear_sky.hdr[i],
                  "a layer without density must not change foreground geometry");
    auto solar_camera = camera;
    check(solar_camera.look_at(solar_camera.position() + glm::vec3{-0.4F, 0.72F, -0.57F}),
          "solar camera must face the directional light");
    solar_camera.set_fov_y(5.0F);
    auto const solar_disc = capture(device, renderer, frame, solar_camera, scene);
    auto const peak =
        std::ranges::max_element(solar_disc.hdr, {}, [](glm::vec4 pixel) { return pixel.r; });
    check(peak->r > 10.0F, "clear sky must show an HDR solar disc towards the sun");
    auto const solar_corner = solar_disc.hdr.front();
    check(solar_corner.r < 1.0F, "solar disc must remain localized in the sky");
    auto const aureole = solar_disc.hdr[32 * test_extent.width + 40];
    check(aureole.r > solar_corner.r + 0.15F,
          "sunlight must brighten the sky outside the disc with a gradual aureole");
    lc1::scene::Scene sunless;
    sunless.set_sky(lc1::scene::Sky{.color = {0.22F, 0.34F, 0.55F}});
    auto const no_sun = capture(device, renderer, frame, solar_camera, sunless);
    for (auto const pixel : no_sun.hdr)
        check(pixel.r < 1.0F, "no directional light must mean no solar disc");
    if (samples == vk::SampleCountFlagBits::e1)
        save(solar_disc, "artifacts/solar-disc.ppm");
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

    // Erosion must open gaps through thin cloud, not just dim the same slab.
    // Freeze advection so both frames sample exactly the same field.
    auto const original_wind = clouds.wind_velocity;
    auto const original_detail = clouds.detail_strength;
    clouds.wind_velocity = glm::vec3{0};
    clouds.detail_strength = 0.0F;
    auto const solid_edges = capture(device, renderer, frame, camera, scene);
    clouds.detail_strength = 1.0F;
    auto const eroded_edges = capture(device, renderer, frame, camera, scene);
    std::size_t opened_gaps = 0;
    for (std::size_t i = 0; i < solid_edges.hdr.size(); ++i)
        if (solid_edges.hdr[i].x > 0.02F && eroded_edges.hdr[i].x < 1e-5F)
            ++opened_gaps;
    clouds.wind_velocity = original_wind;
    clouds.detail_strength = original_detail;

    // The debug views are what a user reads to understand the layer.
    clouds.debug = lc1::CloudSettings::Debug::Density;
    auto const density_view = capture(device, renderer, frame, camera, scene);
    check(mean_radiance(density_view, all) > 0.01F, "the density view must show the cloud body");
    clouds.debug = lc1::CloudSettings::Debug::SunTransmittance;
    auto const sun_view = capture(device, renderer, frame, camera, scene);
    float const sun_mean = mean_radiance(sun_view, all);
    check(sun_mean > 0.05F && sun_mean < 1.0F, "sun transmittance must be partial under the sun");
    // A longer sun column must account for the cloud above the local surface.
    auto const lighting_wind = clouds.wind_velocity;
    clouds.wind_velocity = glm::vec3{0};
    clouds.light_march_distance = 100.0F;
    auto const short_column = capture(device, renderer, frame, camera, scene);
    clouds.light_march_distance = lc1::CloudSettings{}.light_march_distance;
    auto const full_column = capture(device, renderer, frame, camera, scene);
    check(mean_radiance(full_column, all) < mean_radiance(short_column, all) - 0.005F,
          "sun transmittance must include the cloud above the near-surface segment");
    clouds.debug = lc1::CloudSettings::Debug::Off;
    lc1::scene::Scene unlit;
    unlit.set_sky(lc1::scene::Sky{.color = {0.0F, 0.0F, 0.0F}});
    auto const dark_cloud = capture(device, renderer, frame, camera, unlit);
    for (auto const &pixel : dark_cloud.hdr)
        check(glm::length(glm::vec3{pixel}) < 1e-5F,
              "multiple scattering must not create light without sun or sky");
    clouds.wind_velocity = lighting_wind;
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

    {
        std::filesystem::create_directories("artifacts");
        // Fixed wind and ground-facing views make the lower silhouette comparable
        // across density changes, for both single-sample and multisampled depth.
        auto const original_settings = clouds;
        clouds = lc1::CloudSettings{};
        clouds.enabled = true;
        clouds.wind_velocity = glm::vec3{0};
        auto underside_camera = camera;
        underside_camera.set_position({5500.0F, 40.0F, 4000.0F});
        underside_camera.add_pitch(20.0F);
        lc1::OutputSettings const display_output{.exposure = 2.0F,
                                                 .encoding = lc1::OutputEncoding::Srgb};
        save(capture(device, renderer, frame, underside_camera, scene, true, {960, 540}, nullptr,
                     {}, display_output),
             "artifacts/clouds-ground.ppm");
        underside_camera.set_position({5500.0F, 700.0F, 1500.0F});
        underside_camera.add_pitch(35.0F);
        save(capture(device, renderer, frame, underside_camera, scene, true, {960, 540}, nullptr,
                     {}, display_output),
             "artifacts/clouds-underside.ppm");
        auto sunlit_camera = camera;
        sunlit_camera.set_position({5500.0F, 40.0F, -4000.0F});
        sunlit_camera.add_yaw(180.0F);
        sunlit_camera.add_pitch(20.0F);
        save(capture(device, renderer, frame, sunlit_camera, scene, true, {960, 540}, nullptr, {},
                     display_output),
             "artifacts/clouds-sunlit.ppm");
        auto solar_view = solar_camera;
        solar_view.set_fov_y(55.0F);
        solar_view.add_pitch(-12.0F);
        lc1::OutputSettings const solar_output{
            .exposure = 2.0F, .bloom_strength = 0.12F, .encoding = lc1::OutputEncoding::Srgb};
        clouds.density = 0.0F;
        save(capture(device, renderer, frame, solar_view, scene, true, {960, 540}, nullptr, {},
                     solar_output),
             "artifacts/solar-day-clear.ppm");
        clouds.density = 1.0F;
        save(capture(device, renderer, frame, solar_view, scene, true, {960, 540}, nullptr, {},
                     solar_output),
             "artifacts/solar-day-clouds.ppm");
        lc1::scene::Scene sunset;
        sunset.set_sky(lc1::scene::Sky{.color = {0.32F, 0.20F, 0.16F}});
        sunset.add_light(lc1::scene::to_light(
            lc1::scene::DirectionalLight{.base = {.color = {1.0F, 0.96F, 0.9F}, .intensity = 0.12F},
                                         .direction = {-0.98F, -0.18F, 0.0F}}));
        auto sunset_view = camera;
        check(sunset_view.look_at(sunset_view.position() + glm::vec3{0.98F, 0.18F, 0.0F}),
              "sunset camera must face the low sun");
        sunset_view.set_fov_y(55.0F);
        sunset_view.add_pitch(-6.0F);
        clouds.density = 0.0F;
        save(capture(device, renderer, frame, sunset_view, sunset, true, {960, 540}, nullptr, {},
                     solar_output),
             "artifacts/solar-sunset.ppm");
        clouds = original_settings;
    }

    // Readable captures for the layer itself, its absence and its density field. These
    // are review artifacts, not assertions: the behavioural checks are above. They come
    // from the multisampled run when the device has one, matching the game's path.
    if (samples != vk::SampleCountFlagBits::e1) {
        std::filesystem::create_directories("artifacts");
        // Ship defaults, only enabled: these captures are the look a player sees.
        clouds = lc1::CloudSettings{};
        clouds.enabled = true;
        constexpr vk::Extent2D artifact_extent{480, 270};
        double clear_ms = 0.0;
        double cloud_ms = 0.0;
        // From below the layer but well off the ground: a view into the deck rather
        // than along it, where the layer's thickness and structure are visible.
        auto showcase_camera = camera;
        showcase_camera.set_position({0.0F, 5000.0F, 0.0F});
        showcase_camera.add_pitch(glm::radians(-89.0F));
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
                       .color = {8.0F, 8.0F, 8.0F},
                       .normal = showcase_camera.position() - label_center}},
            .screen = {},
        };
        lc1::OutputSettings const showcase_output{.exposure = 2.0F,
                                                  .encoding = lc1::OutputEncoding::Srgb};
        save(capture(device, renderer, frame, showcase_camera, scene, true, artifact_extent,
                     &cloud_ms, text, showcase_output),
             "artifacts/clouds.ppm");

        // Interleaved measurements after warm-up at a fixed ground viewpoint.
        // GPU timestamps cover rendering only; report medians instead of one frame.
        std::array<double, 15> clear_times{};
        std::array<double, 15> cloud_times{};
        auto performance_camera = camera;
        performance_camera.set_position({5500.0F, 40.0F, 4000.0F});
        performance_camera.add_pitch(20.0F);
        auto const performance_wind = clouds.wind_velocity;
        clouds.wind_velocity = glm::vec3{0};
        constexpr vk::Extent2D full_extent{1244, 1516};
        performance_camera.set_aspect_ratio(static_cast<float>(full_extent.width) /
                                            full_extent.height);
        lc1::GpuImage performance_output{device,
                                         vk::Format::eR8G8B8A8Unorm,
                                         full_extent,
                                         1,
                                         vk::SampleCountFlagBits::e1,
                                         vk::ImageUsageFlagBits::eColorAttachment,
                                         vk::ImageAspectFlagBits::eColor};
        auto timing = device.raii().createQueryPool(
            {.queryType = vk::QueryType::eTimestamp, .queryCount = 2});
        auto const time_frame = [&] {
            lc1::one_time_submit(device, [&](lc1::CommandBuffer &commands) {
                performance_output.transition_layout(
                    commands.raii(), vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite);
                commands.raii().resetQueryPool(*timing, 0, 2);
                commands.raii().writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *timing, 0);
                renderer.record(commands, frame, {&performance_output.view(), full_extent}, scene,
                                performance_camera, {});
                commands.raii().writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *timing,
                                                1);
            });
            std::array<std::uint64_t, 2> stamps{};
            check(timing.getResults(0, 2, sizeof(stamps), stamps.data(), sizeof(std::uint64_t),
                                    vk::QueryResultFlagBits::e64 |
                                        vk::QueryResultFlagBits::eWait) == vk::Result::eSuccess,
                  "performance timestamps must be available");
            return static_cast<double>(stamps[1] - stamps[0]) *
                   device.raii_physical().getProperties().limits.timestampPeriod / 1e6;
        };
        for (int i = -30; i < 15; ++i) {
            clouds.enabled = false;
            auto const clear_time = time_frame();
            clouds.enabled = true;
            auto const cloud_time = time_frame();
            if (i >= 0) {
                clear_times[i] = clear_time;
                cloud_times[i] = cloud_time;
            }
        }
        clouds.wind_velocity = performance_wind;
        std::ranges::sort(clear_times);
        std::ranges::sort(cloud_times);
        std::cout << std::format(
            "1244x1516 ground frame median (15 pairs): {:.3f} ms clear, {:.3f} ms cloud "
            "(cloud range {:.3f}..{:.3f} ms)\n",
            clear_times[7], cloud_times[7], cloud_times.front(), cloud_times.back());
        clouds.enabled = false;
        save(capture(device, renderer, frame, showcase_camera, scene, false, artifact_extent,
                     &clear_ms, text, showcase_output),
             "artifacts/clouds-clear.ppm");
        clouds.enabled = true;
        clouds.debug = lc1::CloudSettings::Debug::Density;
        save(capture(device, renderer, frame, showcase_camera, scene, true, artifact_extent),
             "artifacts/clouds-density.ppm");
        clouds.debug = lc1::CloudSettings::Debug::Off;
    }

    check(opened_gaps > 0, "detail erosion must remove thin edges, not just attenuate density");

    // Merged surface fog and sky/cloud rendering must remain independent.
    scene.set_fog(
        lc1::scene::DistanceFog{.radiance = {0.24F, 0.28F, 0.31F}, .start = 60.0F, .end = 320.0F});
    clouds.enabled = false;
    auto const fogged_clear = capture(device, renderer, frame, camera, scene, false);
    clouds.enabled = true;
    auto const fogged_cloud = capture(device, renderer, frame, camera, scene);
    float ground_fog_change = 0.0F;
    for (std::size_t i = test_extent.width * test_extent.height * 3 / 4;
         i < fogged_clear.hdr.size(); ++i) {
        auto const cloud_delta = glm::abs(glm::vec3{fogged_cloud.hdr[i] - fogged_clear.hdr[i]});
        check(cloud_delta.x + cloud_delta.y + cloud_delta.z < 1e-3F,
              "cloud compositing must preserve distance fog on foreground geometry");
        ground_fog_change += glm::length(glm::vec3{fogged_clear.hdr[i] - clear_sky.hdr[i]});
    }
    check(ground_fog_change > 0.1F,
          "surface fog must still affect ground with the sky path present");
    scene.set_fog(std::nullopt);

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
