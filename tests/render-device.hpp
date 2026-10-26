#pragma once

#include "lc1/vk/core/device.hpp"

#include <cstdlib>
#include <cstring>
#include <print>
#include <utility>

namespace lc1::test {

// Shared setup for rendering tests; the surface is borrowed only during selection.
inline Device make_render_device(Instance const &instance, vk::SurfaceKHR surface)
{
    DeviceRequirements requirements{
        .vulkan_version = vk::ApiVersion14,
        .extensions =
            {
                vk::KHRAccelerationStructureExtensionName,
                vk::KHRRayQueryExtensionName,

                vk::KHRDeferredHostOperationsExtensionName,

                vk::EXTDescriptorHeapExtensionName,
                vk::KHRShaderUntypedPointersExtensionName,

                vk::EXTShaderObjectExtensionName,
            },
        .features =
            {
                &vk::PhysicalDeviceFeatures::sampleRateShading,
                &vk::PhysicalDeviceFeatures::samplerAnisotropy,
                // Slang loads heap acceleration-structure addresses as 64-bit integers.
                &vk::PhysicalDeviceFeatures::shaderInt64,
                &vk::PhysicalDeviceVulkan11Features::shaderDrawParameters,
                &vk::PhysicalDeviceVulkan12Features::bufferDeviceAddress,
                &vk::PhysicalDeviceVulkan13Features::synchronization2,
                &vk::PhysicalDeviceVulkan13Features::dynamicRendering,
                &vk::PhysicalDeviceVulkan14Features::maintenance5,
                &vk::PhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure,
                &vk::PhysicalDeviceRayQueryFeaturesKHR::rayQuery,
                &vk::PhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap,
                &vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR::shaderUntypedPointers,
                &vk::PhysicalDeviceShaderObjectFeaturesEXT::shaderObject,
            },
    };
    auto [devices, unavailable_reasons] = filter_physical_devices(instance, requirements);
    auto const *requested = std::getenv("LC1_DEVICE");
    bool const filtered = requested != nullptr && *requested != '\0';
    vk::raii::PhysicalDevice *selected = nullptr;
    std::uint32_t queue_family = 0;
    int best_score = -1;
    for (auto &physical : devices) {
        auto const properties = physical.getProperties();
        if (properties.deviceType == vk::PhysicalDeviceType::eCpu ||
            (filtered && std::strstr(properties.deviceName.data(), requested) == nullptr))
            continue;
        int const score = properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu     ? 300
                          : properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ? 200
                          : properties.deviceType == vk::PhysicalDeviceType::eVirtualGpu    ? 100
                                                                                            : 50;
        auto const families = physical.getQueueFamilyProperties();
        for (std::uint32_t i = 0; i < families.size(); ++i) {
            if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics) ||
                !(families[i].queueFlags & vk::QueueFlagBits::eCompute) ||
                !physical.getSurfaceSupportKHR(i, surface))
                continue;
            if (score > best_score) {
                selected = &physical;
                queue_family = i;
                best_score = score;
            }
            break;
        }
    }
    if (selected == nullptr) {
        if (filtered)
            fail("LC1_DEVICE=\"{}\" matched no usable rendering test device", requested);
        fail("rendering tests require a non-CPU Vulkan 1.4 device with the requested "
             "features and a graphics+compute+present queue");
    }
    for (auto const &extension : selected->enumerateDeviceExtensionProperties()) {
        if (std::strcmp(extension.extensionName.data(), "VK_KHR_portability_subset") == 0)
            requirements.extensions.emplace_back("VK_KHR_portability_subset");
    }
    std::println("Rendering test device: {}", selected->getProperties().deviceName.data());
    return Device{instance, std::move(*selected), queue_family, requirements};
}

} // namespace lc1::test
