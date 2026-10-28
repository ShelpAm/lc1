#include "render-device.hpp"

#include "lc1/game/debug/pbr-showcase.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/scene/gpu-model.hpp"
#include "lc1/scene/lights/directional-light.hpp"
#include "lc1/scene/lights/point-light.hpp"
#include "lc1/scene/lights/sphere-light.hpp"
#include "lc1/scene/lights/spot-light.hpp"
#include "lc1/scene/model-instance.hpp"
#include "lc1/scene/model.hpp"
#include "lc1/vk/render/renderer.hpp"

#include "lc1/game/camera/map-camera-controller.hpp"
#include "lc1/game/continent/view.hpp"

#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/presentation/surface.hpp"
#include "lc1/vk/resources/gpu-font.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"
#include "lc1/window.hpp"

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
                lc1::TextFrameInput text = {})
{
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
        renderer.record(wrapped, frame, {&output.view(), extent}, scene, camera, settings, text);
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
                {{.mesh = 0},
                 {.parent = 0, .transform = {.translation = {-1, 0, 0}}, .mesh = 0},
                 {.transform = {.translation = {100, 0, 0}}, .mesh = 1}},
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

void showcase_test(lc1::Device const &device)
{
    lc1::Renderer renderer{device, {vk::Format::eR8G8B8A8Srgb}, vk::SampleCountFlagBits::e4};
    auto const blocks = lc1::demo::PbrShowcase::spawn_blocks();
    auto const continent = lc1::Continent::make_prototype(blocks);
    for (int z = -25; z <= 25; z += 2)
        check(continent.can_stand(continent.spawn() + glm::vec3{0, 0, z}, 0.3F, 1.8F),
              "showcase obstructs the main road");
    check(!continent.can_stand(continent.spawn() + glm::vec3{-13, 0, 15}, 0.3F, 1.8F),
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
    auto save = [&](Capture const &result, char const *path) {
        check_tone_map(result, 1, true);
        std::ofstream image{path, std::ios::binary};
        image << "P6\n" << result.extent.width << ' ' << result.extent.height << "\n255\n";
        for (std::size_t i = 0; i < result.output.size(); i += 4)
            for (int c = 0; c < 3; ++c)
                image.put(static_cast<char>(result.output[i + c]));
        check(image.good(), "failed to save showcase capture");
    };
    std::filesystem::create_directories("artifacts");
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
              static_cast<std::size_t>(
                  std::ranges::count_if(draws, [](auto const &draw) { return draw.casts_shadow; })),
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
        check(argc == 1 || animation_only, "usage: lc1_pbr_tests [--model-animation]");
        lc1::VulkanLoader loader;
        lc1::Window window{64, 64, "PBR GPU tests"};
        glfwHideWindow(window.handle());
        auto extensions = window.required_instance_extensions();
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        lc1::Instance instance{loader, std::move(extensions), {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        lc1::Surface surface{instance, window};
        auto device = lc1::test::make_render_device(instance, *surface.raii());
        if (animation_only) {
            model_animation_test(device);
            device.wait_idle();
            std::cout << "Model node animation GPU tests passed\n";
            return 0;
        }
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
