#pragma once

#include "lc1/vk/core/common.hpp"
#include "lc1/vk/core/instance.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace lc1 {

using FeatureChain = vk::StructureChain<
    vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
    vk::PhysicalDeviceVulkan14Features, vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
    vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceDescriptorHeapFeaturesEXT,
    vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR, vk::PhysicalDeviceShaderObjectFeaturesEXT>;

namespace detail {

template <class Chain> struct FeatureChainTraits;

// Derive both the member-pointer alternatives and the query from the chain's types.
// Vulkan 1.0 feature bits are nested in PhysicalDeviceFeatures2::features.
template <class... Features>
struct FeatureChainTraits<vk::StructureChain<vk::PhysicalDeviceFeatures2, Features...>> {
    using Feature =
        std::variant<vk::Bool32 vk::PhysicalDeviceFeatures::*, vk::Bool32 Features::*...>;

    static auto query(vk::raii::PhysicalDevice const &physical_device)
    {
        return physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, Features...>();
    }
};

} // namespace detail

using Extension = std::string;
using Feature = detail::FeatureChainTraits<FeatureChain>::Feature;

struct DeviceRequirements {
    std::uint32_t vulkan_version;
    std::vector<Extension> extensions;
    std::vector<Feature> features;
};

template <class Feature> std::string_view to_string(Feature const &feature)
{
    return std::visit(
        []<class T>(T member) -> std::string_view {
            if constexpr (std::same_as<T, vk::Bool32 vk::PhysicalDeviceFeatures::*>) {
                if (member == &vk::PhysicalDeviceFeatures::samplerAnisotropy)
                    return "samplerAnisotropy";
                if (member == &vk::PhysicalDeviceFeatures::fillModeNonSolid)
                    return "fillModeNonSolid";
                if (member == &vk::PhysicalDeviceFeatures::geometryShader)
                    return "geometryShader";
                if (member == &vk::PhysicalDeviceFeatures::tessellationShader)
                    return "tessellationShader";
            }

            if constexpr (std::same_as<T, vk::Bool32 vk::PhysicalDeviceVulkan12Features::*>) {
                if (member == &vk::PhysicalDeviceVulkan12Features::bufferDeviceAddress)
                    return "bufferDeviceAddress";
                if (member == &vk::PhysicalDeviceVulkan12Features::descriptorIndexing)
                    return "descriptorIndexing";
                if (member == &vk::PhysicalDeviceVulkan12Features::timelineSemaphore)
                    return "timelineSemaphore";
            }

            if constexpr (std::same_as<T, vk::Bool32 vk::PhysicalDeviceVulkan13Features::*>) {
                if (member == &vk::PhysicalDeviceVulkan13Features::dynamicRendering)
                    return "dynamicRendering";
                if (member == &vk::PhysicalDeviceVulkan13Features::synchronization2)
                    return "synchronization2";
                if (member == &vk::PhysicalDeviceVulkan13Features::maintenance4)
                    return "maintenance4";
            }

            return "<unknown feature>";
        },
        feature);
}

FeatureChain get_features_chain(vk::raii::PhysicalDevice const &physical_device);

/// @brief Extracts the value of a feature from a feature chain.
template <class Chain> auto &feature_value(Chain &chain, Feature const &feature)
{
    return std::visit(
        [&]<class T>(vk::Bool32 T::*member) -> decltype(auto) {
            if constexpr (std::same_as<T, vk::PhysicalDeviceFeatures>) {
                return chain.template get<vk::PhysicalDeviceFeatures2>().features.*member;
            }
            else {
                return chain.template get<T>().*member;
            }
        },
        feature);
}

struct FilterPhysicalDevicesResult {
    std::vector<vk::raii::PhysicalDevice> available_devices;
    std::map<std::string, std::vector<std::string>> device_unavailable_reasons;
};

FilterPhysicalDevicesResult filter_physical_devices(Instance const &instance,
                                                    DeviceRequirements const &requirements);

} // namespace lc1
