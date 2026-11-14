#include "lc1/vk/render/volumetric-clouds.hpp"

#include "frame-scene.hpp"
#include "lc1/cloud-noise.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/render-graph.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace lc1 {
namespace {

// Linear HDR, opaque-alpha colour with enough dynamic range for a sunlit cloud
// edge; identical to Renderer::hdr_format, which the HDR chain already requires.
constexpr auto cloud_hdr_format = vk::Format::eR16G16B16A16Sfloat;

struct CloudPushData {
    std::uint32_t cloud;
    std::uint32_t depth;
    std::uint32_t shape_noise;
    std::uint32_t noise_sampler;
};

static_assert(sizeof(CloudPushData) == 16);

struct CompositePushData {
    std::uint32_t cloud;
    std::uint32_t hdr;
};

static_assert(sizeof(CompositePushData) == 8);

void validate(CloudSettings const &settings)
{
    auto const finite = [](float value) { return std::isfinite(value); };
    auto const finite_vector = [&](glm::vec3 value) {
        return finite(value.x) && finite(value.y) && finite(value.z);
    };
    if (!finite(settings.bottom) || !finite(settings.top) || settings.top <= settings.bottom)
        fail("cloud layer requires finite heights with top above bottom");
    if (!finite(settings.coverage) || settings.coverage <= 0.0F || settings.coverage > 1.0F)
        fail("cloud coverage must be finite and in (0, 1]");
    if (!finite(settings.density) || settings.density < 0.0F)
        fail("cloud density must be finite and nonnegative");
    if (!finite(settings.noise_scale) || settings.noise_scale <= 0.0F)
        fail("cloud noise scale must be finite and positive");
    if (!finite(settings.detail_scale) || settings.detail_scale < 1.0F ||
        settings.detail_scale > 64.0F)
        fail("cloud detail scale must be finite and in [1, 64]");
    if (!finite(settings.detail_strength) || settings.detail_strength < 0.0F ||
        settings.detail_strength > 1.0F)
        fail("cloud detail strength must be finite and in [0, 1]");
    if (!finite_vector(settings.wind_velocity))
        fail("cloud wind velocity must be finite");
    if (!finite(settings.extinction) || settings.extinction <= 0.0F)
        fail("cloud extinction must be finite and positive");
    if (!finite(settings.scattering_albedo) || settings.scattering_albedo < 0.0F ||
        settings.scattering_albedo > 1.0F)
        fail("cloud scattering albedo must be finite and in [0, 1]");
    if (!finite(settings.anisotropy) || std::abs(settings.anisotropy) >= 1.0F)
        fail("cloud phase anisotropy must be finite and within (-1, 1)");
    if (settings.view_steps == 0 || settings.view_steps > 512)
        fail("cloud view steps must be in [1, 512]");
    if (settings.light_steps == 0 || settings.light_steps > 64)
        fail("cloud light steps must be in [1, 64]");
    if (!finite(settings.light_march_distance) || settings.light_march_distance <= 0.0F)
        fail("cloud light march distance must be finite and positive");
    if (!finite(settings.max_trace_distance) || settings.max_trace_distance <= 0.0F)
        fail("cloud trace distance must be finite and positive");
    if (!finite(settings.ambient_scale) || settings.ambient_scale < 0.0F)
        fail("cloud ambient scale must be finite and nonnegative");
    if (!finite(settings.radiance_scale) || settings.radiance_scale < 0.0F)
        fail("cloud radiance scale must be finite and nonnegative");
}

// Brightest directional light double as the sun: the layer is lit by one source, and
// the scene may also carry point, spot and sphere lights.
void sun_light(FrameScene const &scene, glm::vec3 &direction, glm::vec3 &radiance)
{
    direction = glm::vec3{0.0F, 1.0F, 0.0F};
    radiance = glm::vec3{0.0F};
    float best = 0.0F;
    for (auto const &light : scene.lights) {
        if (light.type != 0)
            continue;
        auto const strength =
            glm::max(light.color, glm::vec3{0.0F}) * std::max(light.intensity, 0.0F);
        auto const magnitude = std::max(strength.x, std::max(strength.y, strength.z));
        if (magnitude <= best)
            continue;
        auto const length = glm::length(light.direction);
        if (length <= 0.0F)
            continue;
        // The upload record stores the propagation direction; the cloud layer needs
        // the direction towards the source.
        direction = -light.direction / length;
        radiance = strength;
        best = magnitude;
    }
}

void draw_fullscreen(CommandBuffer &cmd, GpuImage const &target, GraphicsShaders const &shaders)
{
    auto const &commands = cmd.raii();
    vk::RenderingAttachmentInfo const attachment{
        .imageView = *target.view(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    vk::RenderingInfo rendering{
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent()},
        .layerCount = 1,
    };
    rendering.setColorAttachments(attachment);
    commands.beginRendering(rendering);
    shaders.bind_to_command_buffer(cmd);
    set_graphics_state(commands);
    commands.setViewportWithCount(vk::Viewport{.x = 0.0F,
                                               .y = 0.0F,
                                               .width = static_cast<float>(target.extent().width),
                                               .height = static_cast<float>(target.extent().height),
                                               .minDepth = 0.0F,
                                               .maxDepth = 1.0F});
    commands.setScissorWithCount(vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = target.extent()});
    commands.draw(3, 1, 0, 0);
    commands.endRendering();
}

} // namespace

VolumetricCloudRenderer::VolumetricCloudRenderer(Device const &device)
    : device_{&device}, march_shaders_{device, "shaders/clouds.spv"},
      composite_shaders_{device, "shaders/clouds-composite.spv"},
      origin_{std::chrono::steady_clock::now()}
{
}

void VolumetricCloudRenderer::add_passes(RenderGraph &graph, FrameResources &resources,
                                         vk::Extent2D extent, CloudSettings const &settings,
                                         std::shared_ptr<FrameScene const> scene)
{
    validate(settings);
    // The march reads the depth image the HDR pass produced, so "hdr.color" is a real
    // ordering dependency even though the march never samples that colour.
    graph.add_pass("Cloud ray marching", {"hdr.color"}, {"cloud.result"},
                   [this, &resources, extent, settings, scene](CommandBuffer &cmd) {
                       record_march(cmd, resources, extent, settings, *scene);
                   });
    // The composite reads the marched result and the scene it sits in front of, and
    // produces a new version of the HDR colour rather than overwriting it: one image
    // name is one version.
    graph.add_pass("Cloud composite", {"hdr.color", "cloud.result"}, {"hdr.cloud"},
                   [this, &resources, extent](CommandBuffer &cmd) {
                       record_composite(cmd, resources, extent);
                   });
}

void VolumetricCloudRenderer::ensure_shape_noise()
{
    if (shape_noise_)
        return;

    constexpr auto format = vk::Format::eR8Unorm;
    constexpr auto required = vk::FormatFeatureFlagBits::eSampledImage |
                              vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    if ((device_->raii_physical().getFormatProperties(format).optimalTilingFeatures & required) !=
        required)
        fail("volumetric clouds require a linearly filterable R8_UNORM 3D image");
    auto const limit = device_->raii_physical().getProperties().limits.maxImageDimension3D;
    if (cloud_noise_size > limit)
        fail("cloud shape noise needs {} voxels per axis, above the device limit {}",
             cloud_noise_size, limit);

    auto const voxels = make_cloud_shape_noise();
    vk::Extent3D const extent{cloud_noise_size, cloud_noise_size, cloud_noise_size};
    shape_noise_.emplace(*device_, format, extent, 1, vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                         vk::ImageAspectFlagBits::eColor);

    // One-time upload: the volume is immutable offline data and the wait happens
    // once, when the first cloud frame is registered.
    GpuBuffer staging{*device_, voxels.size(), vk::BufferUsageFlagBits2::eTransferSrc,
                      vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    staging.upload(std::as_bytes(std::span{voxels}));
    one_time_submit(*device_, [&](vk::raii::CommandBuffer const &commands) {
        auto const &volume = *shape_noise_;
        volume.transition_layout(
            commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite);
        vk::BufferImageCopy2 region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageOffset = {.x = 0, .y = 0, .z = 0},
            .imageExtent = extent,
        };
        vk::CopyBufferToImageInfo2 copy{
            .srcBuffer = *staging.raii(),
            .dstImage = *volume.raii(),
            .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        };
        copy.setRegions(region);
        commands.copyBufferToImage2(copy);
        volume.transition_layout(
            commands, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    });
}

void VolumetricCloudRenderer::record_march(CommandBuffer &cmd, FrameResources &resources,
                                           vk::Extent2D extent, CloudSettings const &settings,
                                           FrameScene const &scene)
{
    ensure_shape_noise();
    auto &cloud = resources.cloud_image;
    if (!cloud || cloud->extent() != extent) {
        // The caller waited for this frame slot's fence before reusing it.
        cloud.emplace(*device_, cloud_hdr_format, extent, 1, vk::SampleCountFlagBits::e1,
                      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                      vk::ImageAspectFlagBits::eColor);
    }
    auto &scene_depth = *resources.depth_image;
    auto &heap = *resources.descriptor_heap;
    auto const depth_index = resources.image_index(resources.cloud_depth_index, scene_depth);
    if (!resources.cloud_noise_index)
        resources.cloud_noise_index = heap.allocate_image(*shape_noise_);
    if (!resources.cloud_noise_sampler)
        // Repeat addressing is what makes the single shape tile an endless sky: the
        // world position runs far beyond one tile and must wrap, never clamp.
        resources.cloud_noise_sampler = heap.allocate_sampler(vk::SamplerCreateInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eNearest,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .maxLod = 0,
        });

    glm::vec3 sun_direction;
    glm::vec3 sun_radiance;
    sun_light(scene, sun_direction, sun_radiance);
    // A flat ambient source stands in for multiple scattering; the scene's own sky
    // radiance, which already follows the sun, is a better colour than a constant,
    // and the configured fallback keeps a floor under it.
    // One sky colour feeds the background and the layer's ambient term, so a change
    // to the sky (daylight, dusk, night) moves both together.
    glm::vec3 const ambient = settings.ambient_scale * scene.sky_color;

    auto const inverse_view_projection = glm::inverse(scene.camera.view_projection_matrix());
    auto const elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - origin_);
    CloudData const data{
        .inverse_view_projection = {inverse_view_projection[0], inverse_view_projection[1],
                                    inverse_view_projection[2], inverse_view_projection[3]},
        .camera_position_time = {scene.camera.position(), elapsed.count()},
        .sun_direction_anisotropy = {sun_direction, settings.anisotropy},
        .sun_radiance_albedo = {sun_radiance, settings.scattering_albedo},
        .ambient_extinction = {ambient, settings.extinction},
        .layer = {settings.bottom, settings.top, settings.coverage, settings.density},
        .wind_frequency = {settings.wind_velocity, 1.0F / settings.noise_scale},
        .detail = {settings.detail_scale, settings.detail_strength, 0.0F, 0.0F},
        .march = {settings.max_trace_distance, settings.light_march_distance,
                  settings.radiance_scale, 0.0F},
        .steps = {settings.view_steps, settings.light_steps,
                  static_cast<std::uint32_t>(settings.debug), 0U},
    };
    resources.cloud_buffer.upload(std::as_bytes(std::span{&data, 1}));

    cmd.bind_descriptor_heap(heap);
    CloudPushData const push{
        .cloud = resources.cloud_index,
        .depth = depth_index,
        .shape_noise = *resources.cloud_noise_index,
        .noise_sampler = *resources.cloud_noise_sampler,
    };
    cmd.push_data(std::span{&push, 1});

    // The depth image leaves the HDR pass as an attachment; cloud occlusion is the
    // only reason its contents survive the pass.
    scene_depth.transition_layout(
        cmd.raii(), vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    cloud->transition_layout(
        cmd.raii(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    draw_fullscreen(cmd, *cloud, march_shaders_);
    cloud->transition_layout(
        cmd.raii(), vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
}

void VolumetricCloudRenderer::record_composite(CommandBuffer &cmd, FrameResources &resources,
                                               vk::Extent2D extent)
{
    auto &composited = resources.hdr_cloud_image;
    if (!composited || composited->extent() != extent) {
        composited.emplace(*device_, cloud_hdr_format, extent, 1, vk::SampleCountFlagBits::e1,
                           vk::ImageUsageFlagBits::eColorAttachment |
                               vk::ImageUsageFlagBits::eSampled |
                               vk::ImageUsageFlagBits::eTransferSrc,
                           vk::ImageAspectFlagBits::eColor);
    }
    auto &heap = *resources.descriptor_heap;
    auto const cloud_index =
        resources.image_index(resources.cloud_image_index, *resources.cloud_image);
    // ensure_attachments already published this slot's HDR index for the frame.
    auto const hdr_index = resources.pass_data.hdr_image;
    auto const composited_index = resources.image_index(resources.hdr_cloud_index, *composited);

    cmd.bind_descriptor_heap(heap);
    CompositePushData const push{.cloud = cloud_index, .hdr = hdr_index};
    cmd.push_data(std::span{&push, 1});

    composited->transition_layout(
        cmd.raii(), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    draw_fullscreen(cmd, *composited, composite_shaders_);
    composited->transition_layout(
        cmd.raii(), vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
    // Bloom and tone mapping read the frame's single sampled HDR image through this
    // index; from here on that image is the composited one.
    resources.pass_data.hdr_image = composited_index;
}

} // namespace lc1
