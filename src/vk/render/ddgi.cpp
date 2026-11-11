#include "lc1/vk/render/ddgi.hpp"

#include "frame-scene.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/material.hpp"
#include "lc1/vk/render/render-graph.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lc1 {
namespace {
constexpr vk::Extent2D atlas_extent{512,
                                    1024}; // Four cascades of 64 x 32 tiles, 8 x 8 texels each.
constexpr std::uint32_t probe_count = 2048;
constexpr std::uint32_t ray_count = 64;
constexpr std::array spacings{4.0F, 16.0F, 64.0F, 256.0F};
constexpr std::array batches{64U, 32U, 16U, 16U};

struct DdgiGeometry {
    vk::DeviceAddress vertices;
    vk::DeviceAddress indices;
    HeapIndex object;
    HeapIndex material;
    std::uint32_t index_stride;
    std::uint32_t padding = 0;
};

static_assert(sizeof(DdgiGeometry) == 32);
// GI reads the packed vertex stream through scalar device pointers.
static_assert(sizeof(Vertex) == 60 && offsetof(Vertex, normal) == 12 &&
              offsetof(Vertex, color) == 24 && offsetof(Vertex, uv) == 36);
} // namespace

Ddgi::Ddgi(Device const &device)
    : device_{&device},
      emitter_shaders_{device, "shaders/ddgi-emitter-prepare.spv", "emitterVert", "emitterPrepare"},
      candidate_shaders_{device, "shaders/ddgi-candidates.spv", "candidateVert", "candidateFrag"},
      trace_shaders_{device, "shaders/ddgi-trace.spv", "ddgiVert", "ddgiTrace"},
      blend_shaders_{device, "shaders/ddgi-blend.spv", "ddgiVert", "ddgiBlend"},
      resolve_shaders_{device, "shaders/ddgi-resolve.spv", "ddgiVert", "ddgiResolve"}
{
    constexpr auto required = vk::FormatFeatureFlagBits::eColorAttachment |
                              vk::FormatFeatureFlagBits::eSampledImage |
                              vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    auto const features = device.raii_physical()
                              .getFormatProperties(vk::Format::eR32G32B32A32Sfloat)
                              .optimalTilingFeatures;
    if ((features & required) != required)
        fail("DDGI requires linearly filterable RGBA32F color attachments");
}

void Ddgi::add_passes(RenderGraph &graph, FrameResources &resources, vk::Extent2D extent,
                      std::shared_ptr<FrameScene const> scene)
{
    graph.add_pass("DDGI probes",
                   {"shadow.scene", "shadow.frame", "shadow.surface", "shadow.filtered_visibility",
                    "material.frame"},
                   {"ddgi.irradiance"}, [this, &resources, extent, scene](CommandBuffer &cmd) {
                       resources.ddgi_active = scene->diffuse_gi.has_value();
                       if (resources.ddgi_active)
                           record(cmd, resources, extent, *scene);
                       else
                           valid_ = false;
                   });
}

void Ddgi::record(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent,
                  FrameScene const &scene)
{
    auto const &commands = cmd.raii();
    auto const make_image = [&](vk::Extent2D extent,
                                vk::Format format = vk::Format::eR32G32B32A32Sfloat) {
        return GpuImage{*device_,
                        format,
                        extent,
                        1,
                        vk::SampleCountFlagBits::e1,
                        vk::ImageUsageFlagBits::eColorAttachment |
                            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                        vk::ImageAspectFlagBits::eColor};
    };
    if (!history_[0]) {
        for (auto &history : history_) {
            history.emplace(History{make_image(atlas_extent), make_image(atlas_extent)});
            for (auto *image : {&history->irradiance, &history->distance}) {
                image->transition_layout(
                    commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                    vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eClear,
                    vk::AccessFlagBits2::eTransferWrite);
                commands.clearColorImage(
                    *image->raii(), vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue{0.0F, 0.0F, 0.0F,
                                        image == &history->irradiance ? -1.0F : 0.0F},
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
                image->transition_layout(commands, vk::ImageLayout::eTransferDstOptimal,
                                         vk::ImageLayout::eShaderReadOnlyOptimal,
                                         vk::PipelineStageFlagBits2::eClear,
                                         vk::AccessFlagBits2::eTransferWrite,
                                         vk::PipelineStageFlagBits2::eFragmentShader,
                                         vk::AccessFlagBits2::eShaderSampledRead);
            }
        }
    }
    if (!resources.ddgi_uniform) {
        resources.ddgi_uniform.emplace(*device_, sizeof(DdgiData),
                                       vk::BufferUsageFlagBits2::eUniformBuffer |
                                           vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                                       vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
        resources.ddgi_index = resources.descriptor_cache->buffer(*resources.ddgi_uniform);
        resources.ddgi_geometry.emplace(*device_,
                                        std::max(std::size_t{1}, resources.object_buffers.size()) *
                                            sizeof(DdgiGeometry),
                                        vk::BufferUsageFlagBits2::eStorageBuffer |
                                            vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                                        vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    }
    vk::Extent2D const half_extent{(extent.width + 1) / 2, (extent.height + 1) / 2};
    if (!resources.ddgi_resolved || resources.ddgi_resolved->extent() != half_extent)
        resources.ddgi_resolved.emplace(make_image(half_extent));
    std::vector<DdgiGeometry> geometry;
    std::vector<glm::uvec2> emitters;
    std::vector<std::pair<GpuMesh const *, glm::mat4>> emitter_states;
    geometry.reserve(scene.draws.size());
    for (std::size_t i = 0; i < scene.draws.size(); ++i) {
        auto const &mesh = *scene.draws[i].mesh;
        geometry.push_back({mesh.vertex_address(), mesh.index_address(),
                            resources.object_indices[i], resources.material_indices[i],
                            mesh.index_stride()});
        if (scene.diffuse_gi->emissive_lighting && scene.draws[i].casts_shadow &&
            glm::any(glm::greaterThan(scene.draws[i].material->parameters.emissive_factor,
                                      glm::vec3{0}))) {
            emitter_states.emplace_back(&mesh, scene.draws[i].model_matrix);
            for (std::uint32_t triangle = 0; triangle < mesh.triangle_count(); ++triangle)
                emitters.emplace_back(static_cast<std::uint32_t>(i), triangle);
        }
    }
    auto const emission_samples = static_cast<std::uint32_t>(emitters.size()) * 4U;
    auto const rays_per_cascade = ray_count + emission_samples;
    auto const mask_count = std::max(1U, (emission_samples + 127U) / 128U);
    vk::Extent2D const candidate_extent{64U * mask_count, 128U};
    if (!resources.ddgi_candidates || resources.ddgi_candidates->extent() != candidate_extent) {
        resources.ddgi_candidates.emplace(
            make_image(candidate_extent, vk::Format::eR32G32B32A32Uint));
        resources.ddgi_candidate_origins.reset();
    }
    vk::Extent2D const emission_extent{4, std::max(1U, emission_samples)};
    if (!resources.ddgi_emission || resources.ddgi_emission->extent() != emission_extent)
        resources.ddgi_emission.emplace(make_image(emission_extent));
    auto const count = valid_ ? batches.front() : probe_count;
    vk::Extent2D const rays_extent{rays_per_cascade * 4U, count};
    if (!resources.ddgi_rays || resources.ddgi_rays->extent() != rays_extent)
        resources.ddgi_rays.emplace(make_image(rays_extent));
    auto const emitter_bytes = std::max(sizeof(glm::uvec2), emitters.size() * sizeof(glm::uvec2));
    if (!resources.ddgi_emitters || resources.ddgi_emitters->size() < emitter_bytes)
        resources.ddgi_emitters.emplace(*device_, emitter_bytes,
                                        vk::BufferUsageFlagBits2::eStorageBuffer |
                                            vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                                        vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
    if (!emitters.empty())
        resources.ddgi_emitters->upload(std::as_bytes(std::span{emitters}));
    if (!geometry.empty())
        resources.ddgi_geometry->upload(std::as_bytes(std::span{geometry}));

    auto &heap = *resources.descriptor_heap;
    auto &current = *history_[write_index_];
    auto const &previous = *history_[write_index_ ^ 1U];
    std::array<GpuImage const *, 8> const images{
        &previous.irradiance,      &previous.distance,         &*resources.ddgi_rays,
        &current.irradiance,       &current.distance,          &*resources.ddgi_resolved,
        &*resources.ddgi_emission, &*resources.ddgi_candidates};
    std::array<HeapIndex, 8> indices;
    for (std::size_t i = 0; i < images.size(); ++i)
        indices[i] = resources.image_index(resources.ddgi_images[i], *images[i]);
    vk::SamplerCreateInfo const sampler{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0,
    };
    auto const sampler_index = resources.sampler_index(resources.ddgi_sampler, sampler);
    std::array<glm::vec4, 4> origins;
    for (std::size_t cascade = 0; cascade < spacings.size(); ++cascade) {
        auto const spacing = spacings[cascade];
        auto const origin = glm::floor(scene.diffuse_gi->center / spacing) * spacing -
                            glm::vec3{7.5F * spacing, 3.5F * spacing, 7.5F * spacing};
        origins[cascade] = {origin, spacing};
    }
    DdgiData data{
        .origin_spacing = origins,
        .previous_origin = previous_origins_,
        .sky_radiance = {scene.diffuse_gi->sky_radiance, 0},
        .update = {},
        .inputs = {indices[0], indices[1], indices[2], sampler_index},
        .outputs = {indices[3], indices[4], resources.lights_index, indices[5]},
        .geometry = resources.ddgi_geometry->device_address(),
        .scene = resources.ray_query.scene->address(),
        .resolve = {*resources.surface_index, extent.width, extent.height,
                    *resources.shadow_visibility_index},
        .directions = {},
        .emitters = resources.ddgi_emitters->device_address(),
        .emission_info = {emission_samples, rays_per_cascade},
        .emitter_images = {indices[6], indices[7], mask_count, 0},
    };
    for (std::size_t cascade = 0; cascade < spacings.size(); ++cascade) {
        auto const batch = valid_ ? batches[cascade] : probe_count;
        auto const first = (sequence_ % (probe_count / batch)) * batch;
        data.update[cascade] = {first, batch, sequence_, valid_ ? 1U : 0U};
    }
    // Shared by every probe and every octahedral texel: avoid recomputing these
    // trigonometric terms in hundreds of thousands of shader loop iterations.
    auto const rotation = static_cast<float>(sequence_ % 1024U) * 1.324717957F;
    for (std::uint32_t i = 0; i < ray_count; ++i) {
        auto const y = 1.0F - 2.0F * (static_cast<float>(i) + 0.5F) / ray_count;
        auto const angle = static_cast<float>(i) * 2.39996322973F +
                           static_cast<float>(sequence_ % 1024U) * 0.754877666F;
        auto const r = std::sqrt(std::max(0.0F, 1.0F - y * y));
        glm::vec3 const d{std::cos(angle) * r, y, std::sin(angle) * r};
        data.directions[i] = {d.x, std::cos(rotation) * d.y - std::sin(rotation) * d.z,
                              std::sin(rotation) * d.y + std::cos(rotation) * d.z, 0};
    }
    resources.ddgi_uniform->upload(std::as_bytes(std::span{&data, 1}));
    cmd.bind_descriptor_heap(heap);
    // Local push data is deliberately independent of PassData's shadow fields.
    cmd.push_data(std::span{&*resources.ddgi_index, 1});

    auto &rays = *resources.ddgi_rays;
    rays.transition_layout(
        commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    // All three passes use the same full-screen raster setup. Rays are traced only for
    // this frame's batch; the blend pass scrolls and preserves all other probes.
    auto const draw = [&](GraphicsShaders const &shaders, GraphicsState const &state,
                          vk::Extent2D extent, std::span<GpuImage const *const> targets) {
        std::vector<vk::RenderingAttachmentInfo> attachments;
        for (auto const *image : targets)
            attachments.push_back({.imageView = *image->view(),
                                   .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                   .loadOp = vk::AttachmentLoadOp::eDontCare,
                                   .storeOp = vk::AttachmentStoreOp::eStore});
        vk::RenderingInfo rendering{.renderArea = {{0, 0}, extent}, .layerCount = 1};
        rendering.setColorAttachments(attachments);
        commands.beginRendering(rendering);
        shaders.bind_to_command_buffer(cmd);
        set_graphics_state(commands, state);
        commands.setViewportWithCount(vk::Viewport{.width = static_cast<float>(extent.width),
                                                   .height = static_cast<float>(extent.height),
                                                   .minDepth = 0,
                                                   .maxDepth = 1});
        commands.setScissorWithCount(vk::Rect2D{{0, 0}, extent});
        commands.draw(3, 1, 0, 0);
        commands.endRendering();
    };
    auto &prepared_emission = *resources.ddgi_emission;
    prepared_emission.transition_layout(
        commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    std::array<GpuImage const *, 1> const emission_targets{&prepared_emission};
    draw(emitter_shaders_, trace_state_, emission_extent, emission_targets);
    prepared_emission.transition_layout(
        commands, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
    std::array<GpuImage const *, 1> const ray_targets{&rays};
    draw(trace_shaders_, trace_state_,
         {rays_per_cascade * static_cast<std::uint32_t>(spacings.size()), count}, ray_targets);
    rays.transition_layout(
        commands, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
    for (auto *image : {&current.irradiance, &current.distance})
        image->transition_layout(commands, vk::ImageLayout::eShaderReadOnlyOptimal,
                                 vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::PipelineStageFlagBits2::eFragmentShader |
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eShaderSampledRead |
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite);
    std::array<GpuImage const *, 2> const probe_targets{&current.irradiance, &current.distance};
    draw(blend_shaders_, blend_state_, atlas_extent, probe_targets);
    for (auto *image : {&current.irradiance, &current.distance})
        image->transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eShaderReadOnlyOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eFragmentShader,
                                 vk::AccessFlagBits2::eShaderSampledRead);
    if (resources.ddgi_candidate_origins != origins ||
        resources.ddgi_candidate_emitters != emitter_states) {
        auto &candidates = *resources.ddgi_candidates;
        candidates.transition_layout(
            commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite);
        std::array<GpuImage const *, 1> const candidate_targets{&candidates};
        draw(candidate_shaders_, trace_state_, candidate_extent, candidate_targets);
        candidates.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                     vk::ImageLayout::eShaderReadOnlyOptimal,
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                                     vk::PipelineStageFlagBits2::eFragmentShader,
                                     vk::AccessFlagBits2::eShaderSampledRead);
        resources.ddgi_candidate_origins = origins;
        resources.ddgi_candidate_emitters = std::move(emitter_states);
    }
    auto &resolved = *resources.ddgi_resolved;
    resolved.transition_layout(
        commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    std::array<GpuImage const *, 1> const resolve_targets{&resolved};
    draw(resolve_shaders_, trace_state_, half_extent, resolve_targets);
    resolved.transition_layout(
        commands, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
    previous_origins_ = origins;
    valid_ = true;
    write_index_ ^= 1U;
    ++sequence_;
}
} // namespace lc1
