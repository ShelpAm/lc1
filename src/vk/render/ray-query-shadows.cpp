#include "lc1/vk/render/ray-query-shadows.hpp"
#include "lc1/vk/render/graphics-state.hpp"

#include "draw-validation.hpp"
#include "frame-scene.hpp"
#include "lc1/scene/camera.hpp"
#include "lc1/vk/core/device.hpp"
#include "lc1/vk/render/frame-resources.hpp"
#include "lc1/vk/render/render-graph.hpp"
#include "lc1/vk/resources/command-buffer.hpp"

#include <algorithm>
#include <array>

namespace lc1 {
namespace {

GraphicsShaders make_shadow_visibility_shaders(Device const &device)
{
    auto const required =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    if ((device.raii_physical()
             .getFormatProperties(vk::Format::eR32G32B32A32Uint)
             .optimalTilingFeatures &
         required) != required)
        fail("RGBA32Uint shadow visibility attachments are unsupported");
    return {device, "shaders/visibility.spv", "vertMain", "visibilityMain"};
}

GraphicsShaders make_shadow_temporal_shaders(Device const &device)
{
    auto const required =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    if ((device.raii_physical()
             .getFormatProperties(vk::Format::eR32G32B32A32Sfloat)
             .optimalTilingFeatures &
         required) != required)
        fail("RGBA32Sfloat shadow surface attachments are unsupported");
    return {device, "shaders/temporal-shadows.spv", "temporalVert", "temporalFrag"};
}

} // namespace

RayQueryShadows::RayQueryShadows(Device const &device, vk::Format depth_format)
    : device_{&device}, depth_format_{depth_format},
      visibility_shaders_{make_shadow_visibility_shaders(device)},
      temporal_shaders_{make_shadow_temporal_shaders(device)},
      spatial_shaders_{device, "shaders/spatial-shadows.spv", "spatialVert", "spatialFrag"}
{
}

void RayQueryShadows::add_passes(RenderGraph &graph, FrameResources &resources, vk::Extent2D extent,
                                 std::shared_ptr<FrameScene const> scene)
{
    graph.add_pass("Shadow scene preparation", {"frame.inputs"},
                   {"shadow.scene", "shadow.frame", "shadow.history"},
                   [this, &resources, extent, scene](CommandBuffer &cmd) {
                       prepare_frame(cmd, resources, extent, scene->camera, scene->lights,
                                     scene->draws);
                   });
    graph.add_pass("Shadow visibility", {"shadow.scene", "shadow.frame"},
                   {"shadow.raw_visibility", "shadow.surface"},
                   [this, &resources, extent, scene](CommandBuffer &cmd) {
                       cmd.bind_descriptor_heap(*resources.descriptor_heap);
                       record_visibility(cmd, resources, extent, scene->draws);
                   });
    graph.add_pass("Temporal shadow resolve",
                   {"shadow.raw_visibility", "shadow.surface", "shadow.history", "shadow.frame"},
                   {"shadow.temporal_visibility", "shadow.temporal_surface"},
                   [this, &resources, extent](CommandBuffer &cmd) {
                       cmd.bind_descriptor_heap(*resources.descriptor_heap);
                       resolve_history(cmd, resources, extent);
                   });
    // Temporal resolve must consume raw visibility before spatial filtering
    // overwrites its image. The two resource names describe successive contents.
    graph.add_pass(
        "Spatial shadow filter", {"shadow.temporal_visibility", "shadow.surface", "shadow.frame"},
        {"shadow.filtered_visibility"}, [this, &resources, extent, scene](CommandBuffer &cmd) {
            cmd.bind_descriptor_heap(*resources.descriptor_heap);
            filter_history(cmd, resources, extent);
            finish_frame(scene->camera, scene->lights, scene->draws);
        });
}

void RayQueryShadows::ensure_attachments(FrameResources &resources, vk::Extent2D extent) const
{
    // The caller has waited for these resources. Other submissions use their
    // own resources, so replacing these images needs no device-wide wait here.
    if (!resources.ray_query.visibility || resources.ray_query.visibility->extent() != extent) {
        vk::ImageAspectFlags depth_aspects = vk::ImageAspectFlagBits::eDepth;
        if (depth_format_ == vk::Format::eD32SfloatS8Uint ||
            depth_format_ == vk::Format::eD24UnormS8Uint) {
            // separateDepthStencilLayouts is not enabled: transition both aspects
            // of a combined format even though only depth is attached.
            depth_aspects |= vk::ImageAspectFlagBits::eStencil;
        }
        resources.ray_query.visibility.emplace(
            *device_, vk::Format::eR32G32B32A32Uint, extent, 1, vk::SampleCountFlagBits::e1,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor);
        resources.ray_query.surface.emplace(
            *device_, vk::Format::eR32G32B32A32Sfloat, extent, 1, vk::SampleCountFlagBits::e1,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor);
        resources.ray_query.depth.emplace(
            *device_, depth_format_, extent, 1, vk::SampleCountFlagBits::e1,
            vk::ImageUsageFlagBits::eDepthStencilAttachment, depth_aspects);
    }
}

void RayQueryShadows::record_visibility(CommandBuffer &cmd, FrameResources &resources,
                                        vk::Extent2D extent, std::span<DrawItem const> draws) const
{
    auto const &commands = cmd.raii();
    auto const &visibility = *resources.ray_query.visibility;
    auto const &depth = *resources.ray_query.depth;
    auto const &surface = *resources.ray_query.surface;
    constexpr auto depth_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits2::eLateFragmentTests;
    visibility.transition_layout(
        commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    surface.transition_layout(
        commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    depth.transition_layout(commands, vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eDepthStencilAttachmentOptimal, depth_stages,
                            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, depth_stages,
                            vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
    // A pixel with no center-covered surface defaults to lit for MSAA edge samples.
    vk::RenderingAttachmentInfo const color_attachment{
        .imageView = *visibility.view(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setColor(vk::ClearColorValue{
            std::array<std::uint32_t, 4>{0xffffffffU, 0xffffffffU, 0x0000ffffU, 0U}}),
    };
    vk::RenderingAttachmentInfo const depth_attachment{
        .imageView = *depth.view(),
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue{}.setDepthStencil({.depth = 1.0F, .stencil = 0}),
    };
    vk::RenderingInfo rendering{
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .pDepthAttachment = &depth_attachment,
    };
    vk::RenderingAttachmentInfo const surface_attachment{
        .imageView = *surface.view(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setColor(vk::ClearColorValue{0.0F, 0.0F, 0.0F, 0.0F}),
    };
    std::array const attachments{color_attachment, surface_attachment};
    rendering.setColorAttachments(attachments);
    commands.beginRendering(rendering);
    visibility_shaders_.bind_to_command_buffer(cmd);
    set_graphics_state(commands, visibility_state_);
    commands.setViewportWithCount(vk::Viewport{
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    });
    commands.setScissorWithCount(vk::Rect2D{.offset = {0, 0}, .extent = extent});
    for (std::size_t i = 0; i < draws.size(); ++i) {
        auto const &item = draws[i];
        commands.setFrontFace(glm::determinant(glm::mat3{item.model_matrix}) < 0.0F
                                  ? vk::FrontFace::eClockwise
                                  : vk::FrontFace::eCounterClockwise);
        auto pass = resources.pass_data;
        pass.object = resources.object_indices[i];
        cmd.push_data(std::span{&pass, 1});
        item.mesh->draw(commands);
    }
    commands.endRendering();
    visibility.transition_layout(
        commands, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
    surface.transition_layout(
        commands, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
}

void RayQueryShadows::update_acceleration_structure(vk::raii::CommandBuffer const &commands,
                                                    FrameResources &resources,
                                                    std::span<DrawItem const> draws) const
{
    // Build from every submitted object, including off-camera shadow casters.
    // The current scene submits the entire loaded continent, without view culling.
    std::vector<vk::AccelerationStructureInstanceKHR> instances;
    instances.reserve(draws.size());
    for (auto const &draw : draws) {
        if (!draw.casts_shadow)
            continue;
        vk::AccelerationStructureInstanceKHR instance{};
        // GLM is column-major; Vulkan instances store a row-major affine 3x4.
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                instance.transform.matrix[row][column] = draw.model_matrix[column][row];
        instance.mask = 0xff;
        // Opaque two-sided occluders also handle mirrored instances and thin walls.
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = draw.mesh->acceleration_structure_address();
        instances.push_back(instance);
    }
    bool const rebuild_scene =
        !resources.ray_query.scene || instances != resources.ray_query.built_instances;
    if (rebuild_scene) {
        if (!resources.ray_query.instances)
            resources.ray_query.instances.emplace(
                *device_,
                std::max(std::size_t{1}, resources.object_buffers.size()) *
                    sizeof(instances.front()),
                vk::BufferUsageFlagBits2::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR,
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
        if (!instances.empty())
            resources.ray_query.instances->upload(std::as_bytes(std::span{instances}));
        vk::AccelerationStructureGeometryKHR geometry{
            .geometryType = vk::GeometryTypeKHR::eInstances,
        };
        geometry.geometry.instances = vk::AccelerationStructureGeometryInstancesDataKHR{
            .data = vk::DeviceOrHostAddressConstKHR{device_->raii().getBufferAddress(
                {.buffer = *resources.ray_query.instances->raii()})},
        };
        if (!resources.ray_query.scene) {
            resources.ray_query.scene.emplace(
                *device_, vk::AccelerationStructureTypeKHR::eTopLevel, geometry,
                static_cast<std::uint32_t>(resources.object_buffers.size()));
            // Native AS heap loads return zero on the current Slang/NVIDIA path.
            // A heap UBO preserves the address and the per-frame ownership contract.
            auto const address = resources.ray_query.scene->address();
            resources.ray_query.scene_address.emplace(
                *device_, sizeof(address),
                vk::BufferUsageFlagBits2::eUniformBuffer |
                    vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
            resources.ray_query.scene_address->upload(std::as_bytes(std::span{&address, 1}));
            resources.pass_data.scene =
                resources.descriptor_cache->buffer(*resources.ray_query.scene_address);
        }
        // Order the prior use of this slot's TLAS and scratch before rebuilding.
        vk::MemoryBarrier2 const before_build{
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader |
                            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                             vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                             vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        };
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(before_build));
        resources.ray_query.scene->build(commands, geometry,
                                         static_cast<std::uint32_t>(instances.size()));

        resources.ray_query.built_instances = std::move(instances);
    }
}

void RayQueryShadows::prepare_frame(CommandBuffer &cmd, FrameResources &resources,
                                    vk::Extent2D extent, scene::FpsCamera const &camera,
                                    std::span<scene::Light const> lights,
                                    std::span<DrawItem const> draws)
{
    auto const &command_buffer = cmd.raii();
    validate_draws(resources, extent, draws);
    for (std::size_t i = 0; i < draws.size(); ++i) {
        ObjectData const object_data{
            .model = draws[i].model_matrix,
            .normal_transform = glm::transpose(glm::inverse(draws[i].model_matrix)),
            .shadow_identity = {static_cast<std::uint32_t>(i + 1),
                                i >= previous_draws_.size() ||
                                        previous_draws_[i].mesh != draws[i].mesh ||
                                        previous_draws_[i].model_matrix != draws[i].model_matrix
                                    ? 1U
                                    : 0U,
                                0U, 0U},
        };
        resources.object_buffers[i].upload(std::as_bytes(std::span{&object_data, 1}));
    }

    // Safe to overwrite: the caller has waited for these resources' GPU use.
    CameraData const ubo{
        .view = camera.view_matrix(),
        .proj = camera.projection_matrix(),
        .position = glm::vec4{camera.position(), 1.0F},
    };
    resources.camera_buffer.upload(std::as_bytes(std::span{&ubo, 1}));

    LightsData const lights_data{
        .count = static_cast<std::uint32_t>(std::min(lights.size(), std::size_t{max_num_lights})),
        .lights =
            [&] {
                std::array<scene::Light, max_num_lights> arr{};
                std::copy_n(lights.begin(), std::min(lights.size(), arr.size()), arr.begin());
                return arr;
            }(),
    };
    resources.lights_buffer.upload(std::as_bytes(std::span{&lights_data, 1}));

    update_acceleration_structure(command_buffer, resources, draws);

    ensure_attachments(resources, extent);
    resources.pass_data.current_visibility =
        resources.image_index(resources.shadow_visibility_index, *resources.ray_query.visibility);
    resources.pass_data.current_surface =
        resources.image_index(resources.surface_index, *resources.ray_query.surface);
    prepare_history(command_buffer, resources, extent);
    bool const same_topology =
        draws.size() == previous_draws_.size() &&
        std::equal(draws.begin(), draws.end(), previous_draws_.begin(),
                   [](auto const &a, auto const &b) {
                       return a.mesh == b.mesh && a.casts_shadow == b.casts_shadow;
                   });
    TemporalShadowData const temporal{
        .previous_view_projection = previous_view_projection_,
        .frame_info = {history_valid_ && same_topology &&
                               previous_light_count_ == lights_data.count &&
                               previous_lights_ == lights_data.lights
                           ? 1U
                           : 0U,
                       sequence_++, 32U, lights_data.count},
    };
    resources.temporal_buffer.upload(std::as_bytes(std::span{&temporal, 1}));
}

void RayQueryShadows::finish_frame(scene::FpsCamera const &camera,
                                   std::span<scene::Light const> lights,
                                   std::span<DrawItem const> draws)
{
    history_write_index_ ^= 1U;
    history_valid_ = true;
    previous_view_projection_ = camera.view_projection_matrix();
    previous_draws_.assign(draws.begin(), draws.end());
    previous_lights_ = {};
    previous_light_count_ =
        static_cast<std::uint32_t>(std::min(lights.size(), previous_lights_.size()));
    std::copy_n(lights.begin(), previous_light_count_, previous_lights_.begin());
}

} // namespace lc1
