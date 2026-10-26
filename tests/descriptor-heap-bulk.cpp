#include "lc1/vk/core/device.hpp"
#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/loader.hpp"
#include "lc1/vk/render/graphics-shaders.hpp"
#include "lc1/vk/render/graphics-state.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/descriptor-heap.hpp"
#include "lc1/vk/resources/gpu-buffer.hpp"
#include "lc1/vk/resources/gpu-image.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using Record = std::array<std::uint32_t, 4>;
constexpr std::uint32_t row_width = 1024;
constexpr std::uint32_t buffer_count = 1024;
constexpr std::uint32_t record_count = row_width * buffer_count;
constexpr vk::DeviceSize bulk_bytes = sizeof(Record) * record_count;
constexpr auto format = vk::Format::eR32G32B32A32Uint;
constexpr auto host_write = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
constexpr auto heap_usage =
    vk::BufferUsageFlagBits2::eDescriptorHeapEXT | vk::BufferUsageFlagBits2::eShaderDeviceAddress;

struct PushData {
    std::uint32_t buffer_index;
    std::uint32_t first_record;
    std::uint32_t record_mask;
    std::uint32_t row_width;
};

static_assert(sizeof(Record) == 16 && sizeof(PushData) == 16);

Record value(std::uint32_t record, std::uint32_t buffer, std::uint32_t epoch)
{
    return {record, buffer, epoch,
            (record * 0x9e3779b9U) ^ (buffer * 0x85ebca6bU) ^ (epoch * 0xc2b2ae35U)};
}

std::uint32_t shader_index(std::size_t index)
{
    if (index > std::numeric_limits<std::uint32_t>::max())
        lc1::fail("heap index cannot be represented in shader uint");
    return static_cast<std::uint32_t>(index);
}

lc1::Device make_device(lc1::Instance const &instance)
{
    lc1::DeviceRequirements const requirements{
        .vulkan_version = vk::ApiVersion14,
        .extensions = {vk::EXTDescriptorHeapExtensionName,
                       vk::KHRShaderUntypedPointersExtensionName, vk::EXTShaderObjectExtensionName},
        .features = {&vk::PhysicalDeviceVulkan11Features::shaderDrawParameters,
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
                auto const limits = physical.getProperties().limits;
                std::cout << "Device: " << physical.getProperties().deviceName.data()
                          << "; maxUniformBufferRange=" << limits.maxUniformBufferRange
                          << "; maxStorageBufferRange=" << limits.maxStorageBufferRange << '\n';
                return {instance, std::move(physical), i, requirements};
            }
        }
    }
    lc1::fail("bulk heap test requires a non-CPU Vulkan 1.4 device with descriptor heap support");
}

// Each call finishes GPU use before returning, including before an assertion fails.
// The next epoch may therefore update input buffers without touching an in-flight read.
template <class Bind, class Draw, class Expected>
void verify_pixels(lc1::Device const &device, lc1::GraphicsShaders const &shaders,
                   vk::Extent2D extent, Bind bind, Draw draw, Expected expected,
                   std::string_view label)
{
    lc1::GpuImage output{device,
                         format,
                         extent,
                         1,
                         vk::SampleCountFlagBits::e1,
                         vk::ImageUsageFlagBits::eColorAttachment |
                             vk::ImageUsageFlagBits::eTransferSrc,
                         vk::ImageAspectFlagBits::eColor};
    std::vector<Record> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    auto readback = device.allocator().createBuffer(
        {.size = pixels.size() * sizeof(Record), .usage = vk::BufferUsageFlagBits::eTransferDst},
        {.flags = vma::AllocationCreateFlagBits::eHostAccessRandom,
         .usage = vma::MemoryUsage::eAuto});
    auto pool = device.raii().createCommandPool({.queueFamilyIndex = device.queue_family()});
    auto buffers = device.raii().allocateCommandBuffers(
        {.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    lc1::CommandBuffer command_buffer{std::move(buffers.front())};
    auto fence = device.raii().createFence({});
    auto &commands = command_buffer.raii();
    commands.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    bind(command_buffer);
    output.transition_layout(
        commands, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
    vk::RenderingAttachmentInfo const color{
        .imageView = *output.view(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{}.setColor(vk::ClearColorValue{Record{}})};
    commands.beginRendering(
        vk::RenderingInfo{.renderArea = {.extent = extent}, .layerCount = 1}.setColorAttachments(
            color));
    shaders.bind_to_command_buffer(command_buffer);
    lc1::set_graphics_state(commands);
    commands.setViewportWithCount(vk::Viewport{.width = static_cast<float>(extent.width),
                                               .height = static_cast<float>(extent.height),
                                               .maxDepth = 1});
    commands.setScissorWithCount(vk::Rect2D{.extent = extent});
    if constexpr (std::is_invocable_v<Draw, lc1::CommandBuffer &>)
        draw(command_buffer);
    else
        draw(commands);
    commands.endRendering();
    output.transition_layout(commands, vk::ImageLayout::eColorAttachmentOptimal,
                             vk::ImageLayout::eTransferSrcOptimal,
                             vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                             vk::AccessFlagBits2::eColorAttachmentWrite,
                             vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
    vk::BufferImageCopy const copy{
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .imageExtent = {extent.width, extent.height, 1}};
    commands.copyImageToBuffer(*output.raii(), vk::ImageLayout::eTransferSrcOptimal, *readback,
                               copy);
    vk::MemoryBarrier2 const barrier{.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                     .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                     .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                     .dstAccessMask = vk::AccessFlagBits2::eHostRead};
    commands.pipelineBarrier2(vk::DependencyInfo{}.setMemoryBarriers(barrier));
    commands.end();
    vk::CommandBufferSubmitInfo const command_info{.commandBuffer = *commands};
    device.raii_queue().submit2(vk::SubmitInfo2{}.setCommandBufferInfos(command_info), *fence);
    if (device.raii().waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max()) !=
        vk::Result::eSuccess)
        lc1::fail("bulk heap test fence wait failed");
    readback.getAllocation().copyToMemory(0, pixels.data(), pixels.size() * sizeof(Record));
    for (std::uint32_t y = 0; y < extent.height; ++y)
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            auto const want = expected(x, y);
            auto const &actual = pixels[static_cast<std::size_t>(y) * extent.width + x];
            for (std::size_t c = 0; c < want.size(); ++c)
                if (want[c] != actual[c])
                    lc1::fail("{}: pixel ({}, {}) channel {} expected {}, got {}", label, x, y, c,
                              want[c], actual[c]);
        }
    std::cout << "PASS: " << label << ", checked " << pixels.size() << " uint4 records\n";
}

void test_uniform(lc1::Device const &device)
{
    constexpr vk::DeviceSize uniform_bytes = row_width * sizeof(Record);
    if (device.raii_physical().getProperties().limits.maxUniformBufferRange < uniform_bytes)
        lc1::fail("test requires a 16 KiB uniform buffer range");
    lc1::GraphicsShaders shaders{device, "shaders/descriptor-heap-bulk-test.spv", "vertMain",
                                 "uniformMain"};
    std::vector<lc1::GpuBuffer> buffers;
    buffers.reserve(buffer_count);
    // Nonzero image capacity exercises the buffer partition's offset as well.
    lc1::DescriptorHeap heap{device, 0, 7, buffer_count};
    std::vector<std::uint32_t> indices;
    for (std::uint32_t i = 0; i < buffer_count; ++i) {
        buffers.emplace_back(device, uniform_bytes,
                             vk::BufferUsageFlagBits2::eUniformBuffer |
                                 vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                             host_write);
        indices.push_back(shader_index(heap.allocate_buffer(buffers.back())));
    }
    bool rejected = false;
    try {
        heap.allocate_buffer(buffers.front());
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    if (!rejected)
        lc1::fail("production allocator accepted buffer 1025 beyond capacity 1024");
    std::cout << "Production UBO path: 1024 x 16 KiB = 16 MiB; heap indices " << indices.front()
              << " through " << indices.back() << "; overflow rejected\n";
    std::array<Record, row_width> records;
    for (std::uint32_t epoch = 1; epoch <= 2; ++epoch) {
        for (std::uint32_t i = 0; i < buffer_count; ++i) {
            for (std::uint32_t j = 0; j < row_width; ++j)
                records[j] = value(j, i, epoch);
            buffers[i].upload(std::as_bytes(std::span{records}));
        }
        verify_pixels(
            device, shaders, {row_width, buffer_count},
            [&](lc1::CommandBuffer &commands) { commands.bind_descriptor_heap(heap); },
            [&](lc1::CommandBuffer &wrapped) {
                auto &commands = wrapped.raii();
                for (std::uint32_t y = 0; y < buffer_count; ++y) {
                    auto const buffer = (y * 73 + 11) & (buffer_count - 1);
                    PushData const push{indices[buffer], (y * 13 + 7) & (row_width - 1),
                                        row_width - 1, row_width};
                    wrapped.push_data(std::span{&push, 1});
                    commands.setScissorWithCount(vk::Rect2D{
                        .offset = {0, static_cast<std::int32_t>(y)}, .extent = {row_width, 1}});
                    commands.draw(3, 1, 0, 0);
                }
            },
            [epoch](std::uint32_t x, std::uint32_t y) {
                return value((x * 17 + y * 13 + 7) & (row_width - 1),
                             (y * 73 + 11) & (buffer_count - 1), epoch);
            },
            epoch == 1 ? "production UBO initial contents" : "production UBO updated contents");
    }
}

// Test-only native Vulkan fixture. Production DescriptorHeap::allocate_buffer writes
// uniform descriptors only and cannot express storage descriptors or subranges.
// This verifies the compiler/driver path; it does NOT certify that production API.
void test_storage(lc1::Device const &device)
{
    auto const properties = device.raii_physical()
                                .getProperties2<vk::PhysicalDeviceProperties2,
                                                vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
    auto const &heap_properties = properties.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
    auto const &limits = properties.get<vk::PhysicalDeviceProperties2>().properties.limits;
    if (bulk_bytes > limits.maxStorageBufferRange)
        lc1::fail("test requires a 16 MiB storage buffer range");
    auto const prefix = std::lcm(limits.minStorageBufferOffsetAlignment, vk::DeviceSize{16});
    if ((bulk_bytes / 2) % prefix != 0)
        lc1::fail("half-buffer view does not meet the device storage alignment");
    lc1::GpuBuffer input{device, bulk_bytes + prefix,
                         vk::BufferUsageFlagBits2::eStorageBuffer |
                             vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                         host_write};
    auto const stride = heap_properties.bufferDescriptorSize;
    if (!stride)
        lc1::fail("zero buffer descriptor size");
    auto const first_slot = (heap_properties.minResourceHeapReservedRange + stride - 1) / stride;
    auto const heap_bytes =
        std::max(heap_properties.resourceHeapAlignment, (first_slot + 2) * stride);
    if (heap_bytes > heap_properties.maxResourceHeapSize)
        lc1::fail("native storage fixture exceeds resource heap limit");
    lc1::GpuBuffer resource_heap{device, heap_bytes, heap_usage,
                                 host_write | vma::AllocationCreateFlagBits::eMapped,
                                 heap_properties.resourceHeapAlignment};
    lc1::GpuBuffer sampler_heap{
        device,
        std::max(heap_properties.samplerHeapAlignment, heap_properties.minSamplerHeapReservedRange),
        heap_usage,
        {},
        heap_properties.samplerHeapAlignment};
    for (std::uint32_t view = 0; view < 2; ++view) {
        vk::DeviceAddressRangeEXT const range{.address = input.device_address() + prefix +
                                                         view * (bulk_bytes / 2),
                                              .size = view == 0 ? bulk_bytes : bulk_bytes / 2};
        vk::ResourceDescriptorInfoEXT const info{.type = vk::DescriptorType::eStorageBuffer,
                                                 .data = vk::ResourceDescriptorDataEXT{&range}};
        auto const offset = (first_slot + view) * stride;
        device.raii().writeResourceDescriptorsEXT(
            info, vk::HostAddressRangeEXT{
                      .address = static_cast<std::byte *>(resource_heap.mapped_address()) + offset,
                      .size = stride});
        resource_heap.flush(offset, stride);
    }
    lc1::GraphicsShaders shaders{device, "shaders/descriptor-heap-bulk-test.spv", "vertMain",
                                 "storageMain"};
    auto bind = [&](lc1::CommandBuffer &commands) {
        commands.raii().bindResourceHeapEXT(
            {.heapRange = {.address = resource_heap.device_address(), .size = resource_heap.size()},
             .reservedRangeSize = heap_properties.minResourceHeapReservedRange});
        commands.raii().bindSamplerHeapEXT(
            {.heapRange = {.address = sampler_heap.device_address(), .size = sampler_heap.size()},
             .reservedRangeSize = heap_properties.minSamplerHeapReservedRange});
    };
    if (sizeof(PushData) > heap_properties.maxPushDataSize)
        lc1::fail("test push data exceeds device limit");
    std::cout << "Native storage reference path: 16 MiB full view and 8 MiB suffix view; prefix="
              << prefix << " bytes; production storage allocation remains unsupported\n";
    auto const prefix_records = static_cast<std::size_t>(prefix / sizeof(Record));
    std::vector<Record> records(prefix_records + record_count);
    for (std::uint32_t epoch = 1; epoch <= 2; ++epoch) {
        std::fill(records.begin(), records.end(), Record{0xdeadbeefU, 0, 0, 0});
        for (std::uint32_t i = 0; i < record_count; ++i)
            records[prefix_records + i] = value(i, 0xa5a5U, epoch);
        input.upload(std::as_bytes(std::span{records}));
        for (std::uint32_t view = 0; view < 2; ++view) {
            auto const count = record_count >> view;
            auto const base = view == 0 ? 0U : record_count / 2;
            PushData const push{shader_index(first_slot + view), 137, count - 1, row_width};
            verify_pixels(
                device, shaders, {row_width, count / row_width}, bind,
                [&](vk::raii::CommandBuffer &commands) {
                    commands.pushDataEXT({.data = {.address = &push, .size = sizeof(push)}});
                    commands.draw(3, 1, 0, 0);
                },
                [=](std::uint32_t x, std::uint32_t y) {
                    return value(base + (((y * row_width + x) * 17 + 137) & (count - 1)), 0xa5a5U,
                                 epoch);
                },
                view == 0 ? "native storage full range" : "native storage offset suffix");
        }
    }
}
} // namespace

int main(int argc, char **argv)
{
    try {
        std::cout << std::unitbuf;
        std::string_view const mode = argc == 2 ? argv[1] : "all";
        if (argc > 2 || (mode != "all" && mode != "uniform" && mode != "storage"))
            lc1::fail("usage: lc1_descriptor_heap_bulk_tests [all|uniform|storage]");
        lc1::VulkanLoader loader;
        lc1::Instance instance{
            loader, {vk::EXTDebugUtilsExtensionName}, {"VK_LAYER_KHRONOS_validation"}};
        instance.setup_debug_messenger();
        auto device = make_device(instance);
        if (mode != "storage")
            test_uniform(device);
        if (mode != "uniform")
            test_storage(device);
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
