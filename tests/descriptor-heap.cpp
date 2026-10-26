#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/descriptor-heap-cache.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include "lc1/vk/resources/gpu-texture.hpp"
#include "lc1/vk/resources/one-time-submit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include <span>
#include <vector>

namespace {
constexpr vk::Extent2D extent{256, 256};
constexpr auto format = vk::Format::eR8G8B8A8Unorm;
using Pixel = std::array<std::uint8_t, 4>;
using Triangle = std::array<std::array<float, 4>, 4>;

lc1::Device make_device(lc1::Instance const &instance)
{
    lc1::DeviceRequirements const requirements{
        .vulkan_version = vk::ApiVersion14,
        .extensions = {vk::EXTDescriptorHeapExtensionName,
                       vk::KHRShaderUntypedPointersExtensionName, vk::EXTShaderObjectExtensionName},
        .features = {&vk::PhysicalDeviceFeatures::samplerAnisotropy,
                     &vk::PhysicalDeviceFeatures::sampleRateShading,
                     &vk::PhysicalDeviceVulkan11Features::shaderDrawParameters,
                     &vk::PhysicalDeviceVulkan12Features::bufferDeviceAddress,
                     &vk::PhysicalDeviceVulkan13Features::dynamicRendering,
                     &vk::PhysicalDeviceVulkan13Features::synchronization2,
                     &vk::PhysicalDeviceVulkan14Features::maintenance5,
                     &vk::PhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap,
                     &vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR::shaderUntypedPointers,
                     &vk::PhysicalDeviceShaderObjectFeaturesEXT::shaderObject},
    };
    auto candidates = lc1::filter_physical_devices(instance, requirements);
    for (auto &physical : candidates.available_devices) {
        if (physical.getProperties().deviceType == vk::PhysicalDeviceType::eCpu)
            continue;
        auto const families = physical.getQueueFamilyProperties();
        for (std::uint32_t i = 0; i < families.size(); ++i) {
            if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                std::cout << "Descriptor heap test device: "
                          << physical.getProperties().deviceName.data() << '\n';
                return lc1::Device{instance, std::move(physical), i, requirements};
            }
        }
    }
    lc1::fail("descriptor heap test requires a Vulkan 1.4 GPU with descriptorHeap, "
              "shaderUntypedPointers, shaderObject and sampleRateShading");
}

void run(lc1::Device const &device)
{
    std::array<Triangle, 2> const triangles{{
        {{{-0.9F, 0.7F, 0, 1}, {-0.1F, 0.7F, 0, 1}, {-0.5F, -0.7F, 0, 1}, {1, 1, 1, 1}}},
        {{{0.1F, 0.7F, 0, 1}, {0.9F, 0.7F, 0, 1}, {0.5F, -0.7F, 0, 1}, {1, 1, 1, 1}}},
    }};
    std::vector<lc1::GpuBuffer> data;
    for (auto const &triangle : triangles) {
        data.emplace_back(device, sizeof(Triangle),
                          vk::BufferUsageFlagBits2::eUniformBuffer |
                              vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                          vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
        data.back().upload(std::as_bytes(std::span{triangle}));
    }
    std::array<Pixel, 2> const texels{{{255, 64, 0, 255}, {0, 191, 255, 255}}};
    std::array textures{lc1::GpuTexture{device, {1, 1}, texels[0], lc1::TextureColorSpace::Linear},
                        lc1::GpuTexture{device, {1, 1}, texels[1], lc1::TextureColorSpace::Linear}};
    // Allocate every descriptor through the production allocator. The caller does
    // not know heap offsets, reserved ranges, strides, or descriptor encoding.
    lc1::DescriptorHeap heap{device, 2, 2, 2};
    lc1::DescriptorHeapCache cache{heap};

    struct PushData {
        std::uint32_t buffer_index;
        std::uint32_t image_index;
        std::uint32_t sampler_index;
    };

    static_assert(sizeof(PushData) == 12);
    auto shader_index = [](std::size_t index) {
        if (index > std::numeric_limits<std::uint32_t>::max())
            lc1::fail("heap index exceeds the shader's uint32 index");
        return static_cast<std::uint32_t>(index);
    };
    std::array<PushData, 2> draws;
    for (std::size_t i = 0; i < draws.size(); ++i)
        draws[i] = {shader_index(cache.buffer(data[i])),
                    shader_index(cache.image(textures[i].image())), shader_index(cache.sampler())};
    for (std::size_t i = 0; i < draws.size(); ++i) {
        if (cache.buffer(data[i]) != draws[i].buffer_index ||
            cache.image(textures[i].image()) != draws[i].image_index ||
            cache.sampler() != draws[i].sampler_index)
            lc1::fail("descriptor cache did not reuse an existing index");
    }
    if (draws[0].buffer_index == draws[1].buffer_index ||
        draws[0].image_index == draws[1].image_index ||
        draws[0].sampler_index != draws[1].sampler_index)
        lc1::fail("descriptor cache confused distinct resources or duplicated the fixed sampler");
    // A move changes the C++ object's address, but preserves its Vulkan identity.
    auto moved_buffer = std::move(data[0]);
    if (cache.buffer(moved_buffer) != draws[0].buffer_index)
        lc1::fail("descriptor cache lost a moved buffer");
    data[0] = std::move(moved_buffer);
    auto moved_texture = std::move(textures[0]);
    if (cache.image(moved_texture.image()) != draws[0].image_index)
        lc1::fail("descriptor cache lost a moved image");
    textures[0] = std::move(moved_texture);
    heap.allocate_sampler(); // The cache consumed just one of the two slots.
    auto expect_full = [](auto &&allocate) {
        try {
            allocate();
        }
        catch (lc1::Error const &) {
            return;
        }
        lc1::fail("heap allocation unexpectedly succeeded beyond capacity");
    };
    expect_full([&] { heap.allocate_buffer(data[0]); });
    expect_full([&] { heap.allocate_image(textures[0].image()); });
    expect_full([&] { heap.allocate_sampler(); });
    lc1::DescriptorHeap empty{device, 0, 0, 0};
    expect_full([&] { empty.allocate_buffer(data[0]); });
    expect_full([&] { empty.allocate_image(textures[0].image()); });
    expect_full([&] { empty.allocate_sampler(); });

    lc1::GpuImage output{device,
                         format,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    auto readback =
        device.allocator().createBuffer({.size = extent.width * extent.height * sizeof(Pixel),
                                         .usage = vk::BufferUsageFlagBits::eTransferDst},
                                        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
                                         .usage = vma::MemoryUsage::eAuto});
    lc1::GraphicsShaders shaders{device, "shaders/descriptor-heap-test.spv"};
    auto pool = device.raii().createCommandPool(
        {.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
         .queueFamilyIndex = device.queue_family()});
    auto buffers = device.raii().allocateCommandBuffers(
        {.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    lc1::CommandBuffer command_buffer{std::move(buffers.front())};
    auto fence = device.raii().createFence({});
    std::vector<Pixel> pixels(extent.width * extent.height);
    // Re-record the same wrapper to cover invalidation of its heap binding cache.
    for (std::uint32_t frame = 0; frame < 2; ++frame) {
        command_buffer.reset();
        auto &commands = command_buffer.raii();
        commands.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command_buffer.bind_descriptor_heap(heap);
        output.transition_layout(
            commands,
            frame == 0 ? vk::ImageLayout::eUndefined : vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eCopy,
            vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite);
        vk::RenderingAttachmentInfo const color{
            .imageView = *output.view(),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearValue{vk::ClearColorValue{std::array{0.F, 0.F, 0.F, 1.F}}}};
        commands.beginRendering(vk::RenderingInfo{.renderArea = {.extent = extent}, .layerCount = 1}
                                    .setColorAttachments(color));
        shaders.bind_to_command_buffer(command_buffer);
        lc1::set_graphics_state(commands);
        commands.setViewportWithCount(vk::Viewport{.width = 256, .height = 256, .maxDepth = 1});
        commands.setScissorWithCount(vk::Rect2D{.extent = extent});
        for (auto const &draw : draws) {
            command_buffer.push_data(std::span{&draw, 1});
            commands.draw(3, 1, 0, 0);
        }
        commands.endRendering();
        output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                 vk::ImageLayout::eTransferSrcOptimal,
                                 vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                 vk::AccessFlagBits2::eColorAttachmentWrite,
                                 vk::PipelineStageFlagBits2::eCopy,
                                 vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy const region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {extent.width, extent.height, 1}};
        commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                                   region);
        vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                         .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                         .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                         .dstAccessMask = vk::AccessFlagBits2::eHostRead};
        commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
        commands.end();
        vk::CommandBufferSubmitInfo const command_info{.commandBuffer = *commands};
        device.raii_queue().submit2(vk::SubmitInfo2{}.setCommandBufferInfos(command_info), *fence);
        if (device.raii().waitForFences(*fence, vk::True,
                                        std::numeric_limits<std::uint64_t>::max()) !=
            vk::Result::eSuccess)
            lc1::fail("descriptor heap test fence wait failed");
        device.raii().resetFences(*fence);
        readback.getAllocation().copyToMemory(0, pixels.data(), pixels.size() * sizeof(Pixel));
        auto check = [&](std::uint32_t x, std::uint32_t y, Pixel expected) {
            auto const actual = pixels[y * extent.width + x];
            for (std::size_t channel = 0; channel < actual.size(); ++channel)
                if (std::abs(static_cast<int>(actual[channel]) -
                             static_cast<int>(expected[channel])) > 1)
                    lc1::fail("pixel ({}, {}) channel {}: expected {}, got {}", x, y, channel,
                              expected[channel], actual[channel]);
        };
        check(64, 128, {255, 64, 0, 255});
        check(192, 128, {0, 191, 255, 255});
        check(128, 128, {0, 0, 0, 255});
        check(0, 0, {0, 0, 0, 255});
    }
    // GPU work has completed. Forgetting entries must not reclaim slots or leave
    // failed allocations cached as a bogus index on the second lookup.
    cache.invalidate(data[0]);
    cache.invalidate(textures[0].image());
    for (int attempt = 0; attempt < 2; ++attempt) {
        expect_full([&] { cache.buffer(data[0]); });
        expect_full([&] { cache.image(textures[0].image()); });
    }
    if (cache.buffer(data[1]) != draws[1].buffer_index ||
        cache.image(textures[1].image()) != draws[1].image_index)
        lc1::fail("invalidation removed another resource's cache entry");
    cache.clear();
    for (int attempt = 0; attempt < 2; ++attempt) {
        expect_full([&] { cache.buffer(data[1]); });
        expect_full([&] { cache.image(textures[1].image()); });
        expect_full([&] { cache.sampler(); });
    }
    std::filesystem::create_directories("artifacts");
    std::ofstream image{"artifacts/descriptor-heap.ppm", std::ios::binary};
    image.exceptions(std::ios::failbit | std::ios::badbit);
    image << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    for (auto const &pixel : pixels)
        for (std::size_t channel = 0; channel < 3; ++channel)
            image.put(static_cast<char>(pixel[channel]));
    image.close();
    std::cout
        << "PASS: DescriptorHeap buffer/image/sampler allocation, capacity checks, two "
           "recordings and pixel readback; cache reuse, moves, invalidation and failure retry\n"
           "Image: artifacts/descriptor-heap.ppm\n";
}

void sample_shading_test(lc1::Device const &device)
{
    lc1::GraphicsShaders shaders{device, "shaders/descriptor-heap-test.spv", "sampleVert",
                                 "sampleFrag"};
    lc1::DescriptorHeap heap{device, 0, 0, 0};
    constexpr vk::Extent2D size{8, 8};
    auto const supported =
        device.raii_physical()
            .getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
                                      vk::ImageUsageFlagBits::eColorAttachment, {})
            .sampleCounts;
    if (!(supported & vk::SampleCountFlagBits::e4))
        lc1::fail("sample shading regression requires 4x RGBA8 color attachments");
    // Check fresh recordings at both sample counts, with explicitly poisoned state.
    for (auto const samples :
         {vk::SampleCountFlagBits::e1, vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e1}) {
        lc1::GpuImage output{device,
                             format,
                             size,
                             1,
                             vk::SampleCountFlagBits::e1,
                             vk::ImageUsageFlagBits::eColorAttachment |
                                 vk::ImageUsageFlagBits::eTransferSrc,
                             vk::ImageAspectFlagBits::eColor};
        std::optional<lc1::GpuImage> multisample;
        if (samples != vk::SampleCountFlagBits::e1)
            multisample.emplace(device, format, size, 1, samples,
                                vk::ImageUsageFlagBits::eColorAttachment,
                                vk::ImageAspectFlagBits::eColor);
        auto readback = device.allocator().createBuffer(
            {.size = size.width * size.height * sizeof(Pixel),
             .usage = vk::BufferUsageFlagBits::eTransferDst},
            {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
             .usage = vma::MemoryUsage::eAuto});
        lc1::one_time_submit(device, [&](lc1::CommandBuffer &wrapped) {
            auto const &commands = wrapped.raii();
            wrapped.bind_descriptor_heap(heap);
            for (auto const *image : {&output, multisample ? &*multisample : nullptr})
                if (image)
                    image->transition_layout(commands, vk::ImageLayout::eUndefined,
                                             vk::ImageLayout::eColorAttachmentOptimal,
                                             vk::PipelineStageFlagBits2::eNone, {},
                                             vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                             vk::AccessFlagBits2::eColorAttachmentWrite);
            vk::RenderingAttachmentInfo color{
                .imageView = *output.view(),
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue =
                    vk::ClearValue{}.setColor(vk::ClearColorValue{std::array{0.F, 0.F, 0.F, 0.F}})};
            if (multisample) {
                color.imageView = *multisample->view();
                color.resolveMode = vk::ResolveModeFlagBits::eAverage;
                color.resolveImageView = *output.view();
                color.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
                color.storeOp = vk::AttachmentStoreOp::eDontCare;
            }
            commands.beginRendering(
                vk::RenderingInfo{.renderArea = {.extent = size}, .layerCount = 1}
                    .setColorAttachments(color));
            // Simulate state left by another pass. The shared setup must restore it.
            commands.setRasterizerDiscardEnable(vk::True);
            commands.setRasterizationSamplesEXT(samples == vk::SampleCountFlagBits::e1
                                                    ? vk::SampleCountFlagBits::e4
                                                    : vk::SampleCountFlagBits::e1);
            commands.setSampleMaskEXT(samples, vk::SampleMask{0});
            commands.setColorWriteMaskEXT(0, vk::ColorComponentFlags{});
            shaders.bind_to_command_buffer(wrapped);
            lc1::set_graphics_state(commands, {.samples = samples});
            commands.setViewportWithCount(vk::Viewport{.width = 8, .height = 8, .maxDepth = 1});
            commands.setScissorWithCount(vk::Rect2D{.extent = size});
            commands.draw(3, 1, 0, 0);
            commands.endRendering();
            output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                                     vk::ImageLayout::eTransferSrcOptimal,
                                     vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                     vk::AccessFlagBits2::eColorAttachmentWrite,
                                     vk::PipelineStageFlagBits2::eCopy,
                                     vk::AccessFlagBits2::eTransferRead);
            vk::BufferImageCopy const region{
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                     .layerCount = 1},
                .imageExtent = {size.width, size.height, 1}};
            commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal,
                                       *readback, region);
            vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                             .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                             .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                             .dstAccessMask = vk::AccessFlagBits2::eHostRead};
            commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
        });
        std::array<Pixel, size.width * size.height> pixels;
        readback.getAllocation().copyToMemory(0, pixels.data(), sizeof(pixels));
        float const count = static_cast<float>(samples);
        std::array const expected{(count + 1) / 16, (count + 1) * (2 * count + 1) / 384, 1.F, 1.F};
        for (auto const &pixel : pixels)
            for (std::size_t channel = 0; channel < expected.size(); ++channel)
                if (std::abs(pixel[channel] / 255.F - expected[channel]) > 2.F / 255)
                    lc1::fail("{}x sample shading channel {}: got {}, expected {}", count, channel,
                              pixel[channel] / 255.F, expected[channel]);
    }
    std::cout << "PASS: 1x/4x/1x shader object sample shading, resolved moments and dynamic state "
                 "reset\n";
}

} // namespace

int main()
{
    try {
        lc1::VulkanLoader loader;
        lc1::Instance instance{
            loader, {vk::EXTDebugUtilsExtensionName}, {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        auto device = make_device(instance);
        run(device);
        sample_shading_test(device);
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
