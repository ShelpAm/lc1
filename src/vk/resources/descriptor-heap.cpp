#include "lc1/vk/resources/descriptor-heap.hpp"
#include "lc1/vk/resources/command-buffer.hpp"
#include "lc1/vk/resources/sampler.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace lc1 {

namespace {
constexpr auto mapped_heap = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                             vma::AllocationCreateFlagBits::eMapped;

vk::DeviceSize align_descriptor(vk::DeviceSize offset, vk::DeviceSize stride)
{
    if (stride == 0)
        fail("DescriptorHeap: descriptor size is zero (check device support)");
    auto const padding = (stride - offset % stride) % stride;
    if (offset > std::numeric_limits<vk::DeviceSize>::max() - padding)
        fail("DescriptorHeap: offset overflow");
    return offset + padding;
}

vk::DeviceSize region_end(vk::DeviceSize offset, vk::DeviceSize capacity, vk::DeviceSize stride,
                          vk::DeviceSize limit)
{
    if (stride == 0 || offset > limit || capacity > (limit - offset) / stride)
        fail("DescriptorHeap: capacity exceeds the device heap limit");
    return offset + capacity * stride;
}

constexpr auto descriptor_heap_usage =
    vk::BufferUsageFlagBits2::eShaderDeviceAddress | vk::BufferUsageFlagBits2::eDescriptorHeapEXT;
} // namespace

SamplerHeap::SamplerHeap(Device const &device, vk::DeviceSize capacity,
                         vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties)
    // Not considering feature.minSamplerHeapReservedRangeWithEmbedded here.
    : device_(&device), reserved_size_(descriptor_heap_properties.minSamplerHeapReservedRange),
      offset_(align_descriptor(reserved_size_, descriptor_heap_properties.samplerDescriptorSize)),
      stride_(descriptor_heap_properties.samplerDescriptorSize), capacity_(capacity),
      buffer_(device,
              std::max(descriptor_heap_properties.samplerHeapAlignment,
                       region_end(offset_, capacity, stride_,
                                  descriptor_heap_properties.maxSamplerHeapSize)),
              descriptor_heap_usage, mapped_heap, descriptor_heap_properties.samplerHeapAlignment),
      size_(0)
{
}

HeapIndex SamplerHeap::allocate_sampler()
{
    return allocate_sampler(Sampler::default_create_info(*device_));
}

HeapIndex SamplerHeap::allocate_sampler(vk::SamplerCreateInfo const &info)
{
    if (size_ == capacity_)
        fail("DescriptorHeap: sampler heap is full");
    auto const index = static_cast<HeapIndex>(offset_ / stride_ + size_++);
    try {
        write_sampler(index, info);
    }
    catch (...) {
        --size_;
        throw;
    }
    return index;
}

void SamplerHeap::write_sampler(HeapIndex index, vk::SamplerCreateInfo const &info)
{
    auto const base = offset_ / stride_;
    if (index < base || index - base >= size_)
        fail("DescriptorHeap: sampler index is not allocated");
    auto const offset = vk::DeviceSize{index} * stride_;
    vk::HostAddressRangeEXT const range{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + offset,
        .size = stride_,
    };
    device_->raii().writeSamplerDescriptorsEXT(info, range);
    buffer_.flush(offset, stride_);
}

ResourceHeap::ResourceHeap(Device const &device, vk::DeviceSize image_capacity,
                           vk::DeviceSize buffer_capacity,
                           vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties,
                           vk::DeviceSize acceleration_capacity)
    : device_(&device), reserved_size_(descriptor_heap_properties.minResourceHeapReservedRange),
      image_offset_(
          align_descriptor(reserved_size_, descriptor_heap_properties.imageDescriptorSize)),
      image_stride_(descriptor_heap_properties.imageDescriptorSize),
      image_capacity_(image_capacity),
      buffer_offset_(align_descriptor(region_end(image_offset_, image_capacity, image_stride_,
                                                 descriptor_heap_properties.maxResourceHeapSize),
                                      descriptor_heap_properties.bufferDescriptorSize)),
      buffer_stride_(descriptor_heap_properties.bufferDescriptorSize),
      buffer_capacity_(buffer_capacity),
      acceleration_stride_(acceleration_capacity
                               ? device.raii_physical().getDescriptorSizeEXT(
                                     vk::DescriptorType::eAccelerationStructureKHR)
                               : 1),
      acceleration_offset_(
          align_descriptor(region_end(buffer_offset_, buffer_capacity_, buffer_stride_,
                                      descriptor_heap_properties.maxResourceHeapSize),
                           acceleration_stride_)),
      acceleration_capacity_(acceleration_capacity),
      buffer_(
          device,
          std::max(descriptor_heap_properties.resourceHeapAlignment,
                   region_end(acceleration_offset_, acceleration_capacity_, acceleration_stride_,
                              descriptor_heap_properties.maxResourceHeapSize)),
          descriptor_heap_usage, mapped_heap, descriptor_heap_properties.resourceHeapAlignment),
      image_size_(0), buffer_size_(0)
{
}

HeapIndex ResourceHeap::allocate_image(GpuImage const &image, vk::ImageLayout layout)
{
    if (image_size_ == image_capacity_)
        fail("DescriptorHeap: image heap is full");
    auto const index = static_cast<HeapIndex>(image_offset_ / image_stride_ + image_size_++);
    try {
        write_image(index, image, layout);
    }
    catch (...) {
        --image_size_;
        throw;
    }
    return index;
}

void ResourceHeap::write_image(HeapIndex index, GpuImage const &image, vk::ImageLayout layout)
{
    auto const base = image_offset_ / image_stride_;
    if (index < base || index - base >= image_size_)
        fail("DescriptorHeap: image index is not allocated");
    auto view = image.view_create_info();
    vk::ImageDescriptorInfoEXT const image_info{
        .pView = &view,
        .layout = layout,
    };
    vk::ResourceDescriptorInfoEXT const info{
        .type = vk::DescriptorType::eSampledImage,
        .data = vk::ResourceDescriptorDataEXT{&image_info},
    };
    auto const offset = vk::DeviceSize{index} * image_stride_;
    vk::HostAddressRangeEXT const range{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + offset,
        .size = image_stride_,
    };
    device_->raii().writeResourceDescriptorsEXT(info, range);
    buffer_.flush(offset, image_stride_);
}

HeapIndex ResourceHeap::allocate_buffer(GpuBuffer const &buffer)
{
    if (buffer_size_ == buffer_capacity_)
        fail("DescriptorHeap: buffer heap is full");

    vk::DeviceAddressRangeEXT buffer_range{
        .address = buffer.device_address(),
        .size = buffer.size(),
    };
    vk::ResourceDescriptorInfoEXT resource_info{
        .type = vk::DescriptorType::eUniformBuffer,
        .data = vk::ResourceDescriptorDataEXT{&buffer_range},
    };

    vk::HostAddressRangeEXT sampler_har{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + buffer_offset_ +
                   buffer_stride_ * buffer_size_,
        .size = buffer_stride_,
    };

    device_->raii().writeResourceDescriptorsEXT(resource_info, sampler_har);

    buffer_.flush(buffer_offset_ + buffer_stride_ * buffer_size_, buffer_stride_);
    return static_cast<HeapIndex>(buffer_offset_ / buffer_stride_ + buffer_size_++);
}

DescriptorHeap::DescriptorHeap(Device const &device, vk::DeviceSize sampler_capacity,
                               vk::DeviceSize image_capacity, vk::DeviceSize buffer_capacity,
                               vk::DeviceSize acceleration_capacity)
    : device_(&device), descriptor_heap_properties_(
                            device.raii_physical()
                                .getProperties2<vk::PhysicalDeviceProperties2,
                                                vk::PhysicalDeviceDescriptorHeapPropertiesEXT>()
                                .get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>()),
      sampler_heap_(device, sampler_capacity, descriptor_heap_properties_),
      resource_heap_(device, image_capacity, buffer_capacity, descriptor_heap_properties_,
                     acceleration_capacity)
{
}

void DescriptorHeap::probe() const
{
    auto const &physical = device_->raii_physical();
    auto const extensions = physical.enumerateDeviceExtensionProperties();
    auto const supports_extension = [&](std::string_view name) {
        return std::ranges::any_of(extensions, [&](auto const &extension) {
            return std::string_view{extension.extensionName.data()} == name;
        });
    };
    auto const log = [](std::string_view name, auto value, std::string_view unit = {}) {
        spdlog::info("[DescriptorHeap] {:<48} = {}{}", name, value, unit);
    };

    auto const identity =
        physical
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
    auto const &device_properties = identity.get<vk::PhysicalDeviceProperties2>().properties;
    auto const &driver = identity.get<vk::PhysicalDeviceDriverProperties>();
    spdlog::info("[DescriptorHeap] device = {} ({}, Vulkan {}, driver {} {})",
                 device_properties.deviceName.data(), vk::to_string(device_properties.deviceType),
                 version_string(device_properties.apiVersion), driver.driverName.data(),
                 driver.driverInfo.data());
    spdlog::info(
        "[DescriptorHeap] features below report physical-device support, not logical-device "
        "enablement; region used bytes exclude reserved ranges");

    auto const log_heap = [](std::string_view name, GpuBuffer const &buffer,
                             vk::DeviceSize reserved) {
        spdlog::info("[DescriptorHeap] {}: address=0x{:x}, capacity={} bytes, "
                     "reserved=[0, {}) bytes",
                     name, buffer.device_address(), buffer.size(), reserved);
        if (reserved > buffer.size()) {
            spdlog::warn("[DescriptorHeap] {}: reserved range exceeds heap capacity", name);
        }
    };
    auto const log_region = [](std::string_view name, GpuBuffer const &buffer,
                               vk::DeviceSize offset, vk::DeviceSize end, vk::DeviceSize used,
                               vk::DeviceSize stride) {
        spdlog::info("[DescriptorHeap] {}: range=[{}, {}) bytes, stride={} bytes, used={} bytes",
                     name, offset, end, stride, used);
        if (offset > end || end > buffer.size() || used > end - offset) {
            spdlog::warn("[DescriptorHeap] {}: invalid accounting (expected offset <= end <= "
                         "heap capacity and used <= end - offset); capacity/free bytes unavailable",
                         name);
            return;
        }
        auto const capacity = end - offset;
        spdlog::info("[DescriptorHeap] {}: capacity={} bytes, free={} bytes", name, capacity,
                     capacity - used);
    };
    log_heap("sampler heap", sampler_heap_.buffer_, sampler_heap_.reserved_size_);
    log_region("sampler region", sampler_heap_.buffer_, sampler_heap_.offset_,
               sampler_heap_.buffer_.size(), sampler_heap_.size_ * sampler_heap_.stride_,
               sampler_heap_.stride_);
    log_heap("resource heap", resource_heap_.buffer_, resource_heap_.reserved_size_);
    log_region("image region", resource_heap_.buffer_, resource_heap_.image_offset_,
               resource_heap_.buffer_offset_,
               resource_heap_.image_size_ * resource_heap_.image_stride_,
               resource_heap_.image_stride_);
    log_region("buffer region", resource_heap_.buffer_, resource_heap_.buffer_offset_,
               resource_heap_.acceleration_offset_,
               resource_heap_.buffer_size_ * resource_heap_.buffer_stride_,
               resource_heap_.buffer_stride_);

    log_region("acceleration-structure region", resource_heap_.buffer_,
               resource_heap_.acceleration_offset_, resource_heap_.buffer_.size(),
               resource_heap_.acceleration_size_ * resource_heap_.acceleration_stride_,
               resource_heap_.acceleration_stride_);

    bool const supports_heap = supports_extension(vk::EXTDescriptorHeapExtensionName);
    bool const supports_untyped = supports_extension(vk::KHRShaderUntypedPointersExtensionName);
    log(vk::EXTDescriptorHeapExtensionName, supports_heap);
    log(vk::KHRShaderUntypedPointersExtensionName, supports_untyped);
    if (supports_untyped) {
        auto const features =
            physical.getFeatures2<vk::PhysicalDeviceFeatures2,
                                  vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>();
        log("shaderUntypedPointers",
            features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>()
                    .shaderUntypedPointers == vk::True);
    }
    else {
        spdlog::info(
            "[DescriptorHeap] shaderUntypedPointers = unavailable (extension unsupported)");
    }
    if (!supports_heap) {
        spdlog::warn("[DescriptorHeap] heap features, properties and exact descriptor sizes are "
                     "unavailable (VK_EXT_descriptor_heap unsupported)");
        return;
    }

    auto const features = physical.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                vk::PhysicalDeviceDescriptorHeapFeaturesEXT>();
    auto const &heap_features = features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>();
    log("descriptorHeap", heap_features.descriptorHeap == vk::True);
    log("descriptorHeapCaptureReplay", heap_features.descriptorHeapCaptureReplay == vk::True);
    log("shaderInt64",
        features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64 == vk::True);
    log("samplerAnisotropy",
        features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy == vk::True);

    auto const properties =
        physical.getProperties2<vk::PhysicalDeviceProperties2,
                                vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
    auto const &heap = properties.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
    log("samplerHeapAlignment", heap.samplerHeapAlignment, " bytes");
    log("resourceHeapAlignment", heap.resourceHeapAlignment, " bytes");
    log("maxSamplerHeapSize", heap.maxSamplerHeapSize, " bytes");
    log("maxResourceHeapSize", heap.maxResourceHeapSize, " bytes");
    log("minSamplerHeapReservedRange", heap.minSamplerHeapReservedRange, " bytes");
    log("minSamplerHeapReservedRangeWithEmbedded", heap.minSamplerHeapReservedRangeWithEmbedded,
        " bytes");
    log("minResourceHeapReservedRange", heap.minResourceHeapReservedRange, " bytes");
    log("samplerDescriptorSize", heap.samplerDescriptorSize, " bytes");
    log("imageDescriptorSize", heap.imageDescriptorSize, " bytes");
    log("bufferDescriptorSize", heap.bufferDescriptorSize, " bytes");
    log("samplerDescriptorAlignment", heap.samplerDescriptorAlignment, " bytes");
    log("imageDescriptorAlignment", heap.imageDescriptorAlignment, " bytes");
    log("bufferDescriptorAlignment", heap.bufferDescriptorAlignment, " bytes");
    log("maxPushDataSize", heap.maxPushDataSize, " bytes");
    log("imageCaptureReplayOpaqueDataSize", heap.imageCaptureReplayOpaqueDataSize, " bytes");
    log("maxDescriptorHeapEmbeddedSamplers", heap.maxDescriptorHeapEmbeddedSamplers);
    log("samplerYcbcrConversionCount", heap.samplerYcbcrConversionCount);
    log("sparseDescriptorHeaps", heap.sparseDescriptorHeaps == vk::True);
    log("protectedDescriptorHeaps", heap.protectedDescriptorHeaps == vk::True);

    // Only types accepted by vkGetPhysicalDeviceDescriptorSizeEXT; optional types are gated by
    // their device extensions. Combined image samplers and dynamic buffers are not heap types.
    struct DescriptorType {
        vk::DescriptorType type;
        std::string_view extension;
    };

    constexpr std::array descriptor_types{
        DescriptorType{vk::DescriptorType::eSampler, {}},
        DescriptorType{vk::DescriptorType::eUniformBuffer, {}},
        DescriptorType{vk::DescriptorType::eStorageBuffer, {}},
        DescriptorType{vk::DescriptorType::eSampledImage, {}},
        DescriptorType{vk::DescriptorType::eStorageImage, {}},
        DescriptorType{vk::DescriptorType::eUniformTexelBuffer, {}},
        DescriptorType{vk::DescriptorType::eStorageTexelBuffer, {}},
        DescriptorType{vk::DescriptorType::eInputAttachment, {}},
        DescriptorType{vk::DescriptorType::eAccelerationStructureKHR,
                       vk::KHRAccelerationStructureExtensionName},
        // Include the legacy extension in the inventory without enabling it.
        DescriptorType{vk::DescriptorType::eAccelerationStructureNV,
                       VK_NV_RAY_TRACING_EXTENSION_NAME},
        DescriptorType{vk::DescriptorType::ePartitionedAccelerationStructureNV,
                       vk::NVPartitionedAccelerationStructureExtensionName},
        DescriptorType{vk::DescriptorType::eSampleWeightImageQCOM,
                       vk::QCOMImageProcessingExtensionName},
        DescriptorType{vk::DescriptorType::eBlockMatchImageQCOM,
                       vk::QCOMImageProcessingExtensionName},
        DescriptorType{vk::DescriptorType::eTensorARM, vk::ARMTensorsExtensionName},
    };
    for (auto const &[type, extension] : descriptor_types) {
        if (extension.empty() || supports_extension(extension)) {
            spdlog::info("[DescriptorHeap] exact descriptor size {} = {} bytes",
                         vk::to_string(type), physical.getDescriptorSizeEXT(type));
        }
        else {
            spdlog::info("[DescriptorHeap] exact descriptor size {} = unavailable ({} unsupported)",
                         vk::to_string(type), extension);
        }
    }

    if (supports_extension(vk::ARMTensorsExtensionName)) {
        auto const tensor_properties =
            physical.getProperties2<vk::PhysicalDeviceProperties2,
                                    vk::PhysicalDeviceDescriptorHeapTensorPropertiesARM>();
        auto const &tensor =
            tensor_properties.get<vk::PhysicalDeviceDescriptorHeapTensorPropertiesARM>();
        log("tensorDescriptorSize", tensor.tensorDescriptorSize, " bytes");
        log("tensorDescriptorAlignment", tensor.tensorDescriptorAlignment, " bytes");
        log("tensorCaptureReplayOpaqueDataSize", tensor.tensorCaptureReplayOpaqueDataSize,
            " bytes");
    }
    else {
        spdlog::info(
            "[DescriptorHeap] tensorDescriptorSize, tensorDescriptorAlignment, "
            "tensorCaptureReplayOpaqueDataSize = unavailable (VK_ARM_tensors unsupported)");
    }
}

void DescriptorHeap::bind_to_command_buffer(CommandBuffer &command_buffer) const
{
    command_buffer.raii().bindSamplerHeapEXT(vk::BindHeapInfoEXT{
        .heapRange =
            vk::DeviceAddressRangeEXT{
                .address = sampler_heap_.buffer_.device_address(),
                .size = sampler_heap_.buffer_.size(),
            },
        .reservedRangeOffset = 0,
        .reservedRangeSize = sampler_heap_.reserved_size_,
    });

    command_buffer.raii().bindResourceHeapEXT(vk::BindHeapInfoEXT{
        .heapRange =
            vk::DeviceAddressRangeEXT{
                .address = resource_heap_.buffer_.device_address(),
                .size = resource_heap_.buffer_.size(),
            },
        .reservedRangeOffset = 0,
        .reservedRangeSize = resource_heap_.reserved_size_,
    });
}

HeapIndex DescriptorHeap::allocate_sampler()
{
    return sampler_heap_.allocate_sampler();
}

HeapIndex DescriptorHeap::allocate_image(GpuImage const &image, vk::ImageLayout layout)
{
    return resource_heap_.allocate_image(image, layout);
}

HeapIndex DescriptorHeap::allocate_buffer(GpuBuffer const &buffer)
{
    return resource_heap_.allocate_buffer(buffer);
}

HeapIndex DescriptorHeap::allocate_sampler(vk::SamplerCreateInfo const &info)
{
    return sampler_heap_.allocate_sampler(info);
}

void DescriptorHeap::write_sampler(HeapIndex index, vk::SamplerCreateInfo const &info)
{
    sampler_heap_.write_sampler(index, info);
}

void DescriptorHeap::write_image(HeapIndex index, GpuImage const &image, vk::ImageLayout layout)
{
    resource_heap_.write_image(index, image, layout);
}

HeapIndex ResourceHeap::allocate_acceleration_structure(vk::DeviceAddress address)
{
    if (acceleration_size_ == acceleration_capacity_)
        fail("DescriptorHeap: acceleration-structure heap is full");
    if (!address)
        fail("DescriptorHeap: acceleration-structure address is null");
    // A zero size is permitted for an AS address range; no backing-buffer size is guessed.
    vk::DeviceAddressRangeEXT const range{.address = address, .size = 0};
    vk::ResourceDescriptorInfoEXT const info{
        .type = vk::DescriptorType::eAccelerationStructureKHR,
        .data = vk::ResourceDescriptorDataEXT{&range},
    };
    auto const offset = acceleration_offset_ + acceleration_size_ * acceleration_stride_;
    vk::HostAddressRangeEXT const destination{
        .address = static_cast<std::byte *>(buffer_.mapped_address()) + offset,
        .size = acceleration_stride_,
    };
    device_->raii().writeResourceDescriptorsEXT(info, destination);
    buffer_.flush(offset, acceleration_stride_);
    ++acceleration_size_;
    return static_cast<HeapIndex>(offset / acceleration_stride_);
}

HeapIndex DescriptorHeap::allocate_acceleration_structure(vk::DeviceAddress address)
{
    return resource_heap_.allocate_acceleration_structure(address);
}

} // namespace lc1

namespace lc1 {

void SamplerHeap::deallocate_sampler(HeapIndex index)
{
    (void)index;
}

void ResourceHeap::deallocate_image(HeapIndex index)
{
    (void)index;
}

void ResourceHeap::deallocate_buffer(HeapIndex index)
{
    (void)index;
}

} // namespace lc1
