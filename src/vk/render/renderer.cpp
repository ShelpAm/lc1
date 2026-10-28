#include "lc1/vk/render/renderer.hpp"
#include "draw-validation.hpp"
#include "frame-scene.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/render/render-graph.hpp"

#include "lc1/vk/core/device.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace lc1 {
namespace {

vk::Format single_color_format(std::vector<vk::Format> const &formats)
{
    if (formats.size() != 1) {
        fail("renderer requires exactly one color attachment format");
    }
    return formats.front();
}

vk::SampleCountFlagBits hdr_sample_count(Device const &device, vk::Format depth_format,
                                         vk::SampleCountFlagBits requested)
{
    auto const &physical = device.raii_physical();
    auto const hdr_features =
        physical.getFormatProperties(Renderer::hdr_format).optimalTilingFeatures;
    constexpr auto required =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    if ((hdr_features & required) != required)
        fail("RGBA16F color attachment and sampling are required for HDR rendering");
    auto const resolve = physical.getImageFormatProperties(
        Renderer::hdr_format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferSrc,
        {});
    if (!(resolve.sampleCounts & vk::SampleCountFlagBits::e1))
        fail("single-sampled HDR resolve attachment is unsupported");
    auto const color = physical.getImageFormatProperties(
        Renderer::hdr_format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        {});
    auto const depth = physical.getImageFormatProperties(
        depth_format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment, {});
    auto const supported = color.sampleCounts & depth.sampleCounts;
    for (auto const count :
         {vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2, vk::SampleCountFlagBits::e1})
        if (count <= requested && (supported & count))
            return count;
    fail("no supported HDR/depth sample count");
}

constexpr std::array<std::uint8_t, 4> white_pixel{255, 255, 255, 255};
constexpr std::array<std::uint8_t, 4> normal_pixel{128, 128, 255, 255};

} // namespace

Renderer::Renderer(Device const &device, std::vector<vk::Format> const &color_attachment_formats,
                   vk::SampleCountFlagBits samples)
    : device_{&device}, color_format_{single_color_format(color_attachment_formats)},
      depth_format_{find_depth_format(device)},
      main_state_{.vertex_input = VertexInput::Mesh,
                  .samples = hdr_sample_count(device, depth_format_, samples),
                  .depth_test = true},
      shaders_{device, "shaders/shader.spv"}, tone_map_shaders_{device, "shaders/tone-map.spv"},
      text_pass_{device, main_state_.samples}, ray_query_shadows_{device, depth_format_},
      default_sampler_info_{Sampler::default_create_info(device)},
      white_srgb_(device, {.width = 1, .height = 1}, white_pixel, TextureColorSpace::Srgb),
      white_linear_(device, {.width = 1, .height = 1}, white_pixel, TextureColorSpace::Linear),
      flat_normal_(device, {.width = 1, .height = 1}, normal_pixel, TextureColorSpace::Linear)
{
}

FrameResources Renderer::make_frame_resources(std::uint32_t object_capacity) const
{
    if (object_capacity > 65534U) {
        fail("object capacity exceeds 16-bit temporal surface IDs");
    }
    auto const make_buffer = [&](vk::DeviceSize size) {
        return GpuBuffer{*device_, size,
                         vk::BufferUsageFlagBits2::eUniformBuffer |
                             vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                         vma::AllocationCreateFlagBits::eHostAccessSequentialWrite};
    };
    // Samplers: five material slots per draw plus the texture cache's default and
    // the text pass's clamped font sampler. Images: the same five per draw, the
    // HDR resolve, shadow visibility and the font atlas. Buffers: three per draw,
    // the camera/lights/temporal/tone-mapping uniforms and the TLAS address.
    auto heap =
        std::make_unique<DescriptorHeap>(*device_, 5ULL * object_capacity + 2,
                                         5ULL * object_capacity + 8, 3ULL * object_capacity + 6);
    FrameResources result{std::move(heap), make_buffer(sizeof(CameraData)),
                          make_buffer(sizeof(LightsData)), make_buffer(sizeof(TemporalShadowData)),
                          make_buffer(sizeof(ToneMapData))};
    result.camera_index = result.descriptor_cache->buffer(result.camera_buffer);
    result.lights_index = result.descriptor_cache->buffer(result.lights_buffer);
    result.pass_data.camera = result.camera_index;
    result.pass_data.lights = result.lights_index;
    result.pass_data.temporal = result.descriptor_cache->buffer(result.temporal_buffer);
    result.pass_data.tone_map = result.descriptor_cache->buffer(result.tone_map_buffer);
    result.frame_buffers.reserve(object_capacity);
    result.object_buffers.reserve(object_capacity);
    result.material_buffers.reserve(object_capacity);
    result.material_textures.resize(object_capacity);
    for (std::uint32_t i = 0; i < object_capacity; ++i) {
        result.frame_buffers.push_back(make_buffer(sizeof(FrameResources::FrameBufferObject)));
        result.object_buffers.push_back(make_buffer(sizeof(FrameResources::ObjectBufferObject)));
        result.material_buffers.push_back(
            make_buffer(sizeof(FrameResources::MaterialBufferObject)));
        result.frame_indices.push_back(
            result.descriptor_cache->buffer(result.frame_buffers.back()));
        result.object_indices.push_back(
            result.descriptor_cache->buffer(result.object_buffers.back()));
        result.material_indices.push_back(
            result.descriptor_cache->buffer(result.material_buffers.back()));
    }
    return result;
}

void Renderer::ensure_attachments(FrameResources &resources, vk::Extent2D extent) const
{
    // The caller has waited for these resources. Other submissions use their
    // own resources, so replacing these images needs no device-wide wait here.
    if (!resources.depth_image || resources.depth_image->extent() != extent) {
        vk::ImageAspectFlags depth_aspects = vk::ImageAspectFlagBits::eDepth;
        if (depth_format_ == vk::Format::eD32SfloatS8Uint ||
            depth_format_ == vk::Format::eD24UnormS8Uint) {
            // separateDepthStencilLayouts is not enabled: transition both aspects
            // of a combined format even though only depth is attached.
            depth_aspects |= vk::ImageAspectFlagBits::eStencil;
        }
        resources.depth_image.emplace(*device_, depth_format_, extent, 1, main_state_.samples,
                                      vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                      depth_aspects);
    }
    if (main_state_.samples != vk::SampleCountFlagBits::e1 &&
        (!resources.color_image || resources.color_image->extent() != extent)) {
        resources.color_image.emplace(*device_, hdr_format, extent, 1, main_state_.samples,
                                      vk::ImageUsageFlagBits::eColorAttachment |
                                          vk::ImageUsageFlagBits::eTransientAttachment,
                                      vk::ImageAspectFlagBits::eColor);
    }
    if (!resources.hdr_image || resources.hdr_image->extent() != extent) {
        // The caller waited this slot's fence before replacing the image.
        resources.hdr_image.emplace(*device_, hdr_format, extent, 1, vk::SampleCountFlagBits::e1,
                                    vk::ImageUsageFlagBits::eColorAttachment |
                                        vk::ImageUsageFlagBits::eSampled |
                                        vk::ImageUsageFlagBits::eTransferSrc,
                                    vk::ImageAspectFlagBits::eColor);
        resources.pass_data.hdr_image =
            resources.image_index(resources.hdr_index, *resources.hdr_image);
    }
}

void Renderer::record(CommandBuffer &cmd, FrameResources &resources, RenderTarget const &target,
                      scene::Scene const &scene, scene::FpsCamera const &camera,
                      OutputSettings output, TextFrameInput text)
{
    // Callbacks borrow this invocation's inputs and are destroyed before it returns.
    RenderGraph graph;
    add_passes(graph, resources, target, scene, camera, output, std::move(text));
    if (auto result = graph.compile(); !result)
        fail("Cannot compile render graph: {}", result.error());
    graph.execute(cmd);
}

void Renderer::add_passes(RenderGraph &graph, FrameResources &resources, RenderTarget const &target,
                          scene::Scene const &input, scene::FpsCamera const &camera,
                          OutputSettings output, TextFrameInput text)
{
    auto lights = input.lights();
    auto draws = input.draws();
    validate_draws(resources, target.extent, draws);
    if (lights.size() > max_num_lights)
        fail("light count exceeds supported capacity {}", max_num_lights);

    if (!std::isfinite(output.exposure) || output.exposure <= 0.0F)
        fail("exposure must be finite and positive");
    if (output.encoding == OutputEncoding::Srgb && color_format_ != vk::Format::eR8G8B8A8Unorm &&
        color_format_ != vk::Format::eB8G8R8A8Unorm)
        fail("manual sRGB output requires an RGBA8/BGRA8 UNORM target");

    graph.import_resource("frame.inputs");
    graph.import_resource("output.target");
    auto const scene = std::make_shared<FrameScene const>(FrameScene{
        .camera = camera,
        .lights = std::move(lights),
        .draws = std::move(draws),
    });
    ray_query_shadows_.add_passes(graph, resources, target.extent, scene);
    graph.add_pass(
        "HDR lighting", {"shadow.frame", "shadow.filtered_visibility"}, {"hdr.color"},
        [this, &resources, extent = target.extent, scene, text](CommandBuffer &commands) {
            record_hdr(commands, resources, extent, scene->draws, scene->camera, text);
        });
    graph.add_pass("Tone mapping", {"hdr.color", "output.target"}, {"output.color"},
                   [this, &resources, target, output](CommandBuffer &commands) {
                       tone_map(commands, resources, target, output);
                   });
    // Always registered, even with nothing to draw: the overlay passes consume its
    // output name, and a conditionally absent producer would not compile.
    graph.add_pass("UI text", {"output.color"}, {"output.ui_text"},
                   [this, &resources, target, text](CommandBuffer &commands) {
                       record_screen_text(commands, resources, target, text);
                   });
}

void Renderer::record_hdr(CommandBuffer &cmd, FrameResources &resources, vk::Extent2D extent,
                          std::span<DrawItem const> draws, scene::FpsCamera const &camera,
                          TextFrameInput const &text)
{
    auto &command_buffer = cmd.raii();
    ensure_attachments(resources, extent);
    auto &heap = *resources.descriptor_heap;
    auto const &visibility = *resources.ray_query.visibility;
    if (resources.shadow_visibility_index)
        heap.write_image(*resources.shadow_visibility_index, visibility);
    else
        resources.shadow_visibility_index = heap.allocate_image(visibility);
    cmd.bind_descriptor_heap(heap);

    GpuImage const &depth_image = *resources.depth_image;

    constexpr auto depth_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits2::eLateFragmentTests;
    // Depth is cleared every frame, so discard its old contents. Keep the
    // dependency on earlier depth writes when reusing an existing image.
    depth_image.transition_layout(command_buffer, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eDepthStencilAttachmentOptimal, depth_stages,
                                  vk::AccessFlagBits2::eDepthStencilAttachmentWrite, depth_stages,
                                  vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                      vk::AccessFlagBits2::eDepthStencilAttachmentWrite);

    vk::RenderingInfo rendering{
        .renderArea =
            {
                .offset = vk::Offset2D{.x = 0, .y = 0},
                .extent = extent,
            },
        // must not be 0: VUID-VkRenderingInfo-viewMask-06069
        .layerCount = 1,
    };
    constexpr vk::ClearColorValue clear_color{0.24F, 0.42F, 0.64F, 1.0F};

    // Preserve the dependency on prior shader reads when this frame slot is reused.
    resources.hdr_image->transition_layout(
        command_buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    vk::RenderingAttachmentInfo color_attachment{
        .imageView = *resources.hdr_image->view(),
        // The HDR barrier above establishes this layout before rendering.
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setColor(clear_color),
    };
    if (resources.color_image) {
        resources.color_image->transition_layout(
            command_buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite);
        color_attachment.imageView = *resources.color_image->view();
        color_attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
        color_attachment.resolveImageView = *resources.hdr_image->view();
        color_attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        // Only the single-sampled output must survive the rendering pass.
        color_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    }
    rendering.setColorAttachments(color_attachment);

    vk::RenderingAttachmentInfo const depth_attachment{
        .imageView = *depth_image.view(),
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue{}.setDepthStencil({.depth = 1.0F, .stencil = 0}),
    };
    rendering.setPDepthAttachment(&depth_attachment);

    command_buffer.beginRendering(rendering);

    shaders_.bind_to_command_buffer(cmd);
    set_graphics_state(command_buffer, main_state_);
    // Render area, viewport and scissor use the same caller-provided extent.
    // Negative height flips Y for glm, whose clip-space Y points up where
    // Vulkan's points down. y moves to the bottom edge to match: with y = 0,
    // the flipped viewport would cover [-height, 0], entirely off-screen.
    command_buffer.setViewportWithCount(vk::Viewport{
        .x = 0.0F,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    });
    command_buffer.setScissorWithCount(vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = extent});

    for (std::size_t i = 0; i < draws.size(); ++i) {
        // RayQueryShadows already uploaded these object transforms and receiver IDs.
        auto const &info = *draws[i].material;
        auto const &p = info.parameters;
        std::array const textures{info.base_color, info.metallic_roughness, info.normal,
                                  info.occlusion, info.emissive};
        std::array const fallbacks{&white_srgb_, &white_linear_, &flat_normal_, &white_linear_,
                                   &white_srgb_};
        std::array<HeapIndex, 5> images;
        std::array<HeapIndex, 5> samplers;
        for (std::size_t slot = 0; slot < textures.size(); ++slot) {
            auto const &texture =
                textures[slot].texture ? *textures[slot].texture : *fallbacks[slot];
            auto const sampler_info = textures[slot].sampler ? textures[slot].sampler->create_info()
                                                             : default_sampler_info_;
            auto &indices = resources.material_textures[i][slot];
            if (indices.image)
                heap.write_image(*indices.image, texture.image());
            else
                indices.image = heap.allocate_image(texture.image());
            if (indices.sampler)
                heap.write_sampler(*indices.sampler, sampler_info);
            else
                indices.sampler = heap.allocate_sampler(sampler_info);
            images[slot] = *indices.image;
            samplers[slot] = *indices.sampler;
        }
        FrameResources::MaterialBufferObject const material_bo{
            .base_color_factor = p.base_color_factor,
            .metallic_factor = p.metallic_factor,
            .roughtness_factor = p.roughness_factor,
            .base_color_texture_index = images[0],
            .base_color_texture_sampler_index = samplers[0],
            .metallic_roughness_texture_index = images[1],
            .metallic_roughness_texture_sampler_index = samplers[1],
            .normal_scale = p.normal_scale,
            .normal_texture_index = images[2],
            .normal_texture_sampler_index = samplers[2],
            .occlusion_strength = p.occlusion_strength,
            .occlusion_texture_index = images[3],
            .occlusion_texture_sampler_index = samplers[3],
            .emissive_factor = p.emissive_factor,
            .emissive_texture_index = images[4],
            .emissive_texture_sampler_index = samplers[4],
            .flags = info.normal.texture ? 1U : 0U,
        };
        resources.material_buffers[i].upload(std::as_bytes(std::span{&material_bo, 1}));
        FrameResources::FrameBufferObject const frame_bo{
            .clear_color = {},
            .camera_index = resources.camera_index,
            .lights_index = resources.lights_index,
            .object_data_index = resources.object_indices[i],
            .material_index = resources.material_indices[i],
            .shadow_visibility_index = *resources.shadow_visibility_index,
        };
        resources.frame_buffers[i].upload(std::as_bytes(std::span{&frame_bo, 1}));
        FrameResources::PushData const push_data{.frame_buffer_index = resources.frame_indices[i]};
        cmd.push_data(std::span{&push_data, 1});

        DrawItem const &item = draws[i];
        // Roads are separate decal draws. Negative constant/slope bias wins the depth
        // test against the terrain at distance, where a centimetre of geometric lift is
        // below the available depth resolution.
        command_buffer.setDepthBiasEnable(item.depth_bias ? vk::True : vk::False);
        if (item.depth_bias)
            command_buffer.setDepthBias(-1.25F, 0.0F, -1.75F);
        else
            command_buffer.setDepthBias(0.0F, 0.0F, 0.0F);
        // A reflected model reverses winding; callers never need to flip cull mode.
        command_buffer.setFrontFace(glm::determinant(glm::mat3{item.model_matrix}) < 0.0F
                                        ? vk::FrontFace::eClockwise
                                        : vk::FrontFace::eCounterClockwise);
        item.mesh->draw(command_buffer);
    }

    // World text closes the scene: it shares this scope's colour and depth
    // attachments, so it is occluded like any other scene geometry, needs no
    // second resolve, and the transient MSAA attachment keeps loadOp Clear.
    if (text.font != nullptr && !text.world.empty())
        text_pass_.draw_world(cmd, resources, *text.font, text.world,
                              camera.view_projection_matrix());

    command_buffer.endRendering();
    resources.hdr_image->transition_layout(
        command_buffer, vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead);
}

void Renderer::record_screen_text(CommandBuffer &cmd, FrameResources &resources,
                                  RenderTarget const &target, TextFrameInput const &text)
{
    if (text.font == nullptr || text.screen.empty())
        return;
    auto &command_buffer = cmd.raii();
    // Make tone mapping's writes visible to this attachment load and blending, as
    // the ImGui overlay does for its own draws.
    vk::MemoryBarrier2 const barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask =
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
    };
    command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
    vk::RenderingAttachmentInfo const color{
        .imageView = **target.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    vk::RenderingInfo rendering{
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
        .layerCount = 1,
    };
    rendering.setColorAttachments(color);
    command_buffer.beginRendering(rendering);
    command_buffer.setViewportWithCount(vk::Viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(target.extent.width),
        .height = static_cast<float>(target.extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    });
    command_buffer.setScissorWithCount(
        vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = target.extent});
    text_pass_.draw_screen(cmd, resources, target.extent, *text.font, text.screen);
    command_buffer.endRendering();
}

} // namespace lc1

namespace lc1 {

vk::SampleCountFlagBits Renderer::sample_count() const
{
    return main_state_.samples;
}

} // namespace lc1
