#include "lc1/vk/core/physical-device.hpp"

namespace lc1 {

FeatureChain get_features_chain(vk::raii::PhysicalDevice const &physical_device)
{
    return detail::FeatureChainTraits<FeatureChain>::query(physical_device);
}

FilterPhysicalDevicesResult filter_physical_devices(Instance const &instance,
                                                    DeviceRequirements const &requirements)
{
    auto get_available_extensions = [](vk::raii::PhysicalDevice const &physical_device) {
        std::set<Extension> available_extensions;
        for (auto const &ext_props : physical_device.enumerateDeviceExtensionProperties()) {
            available_extensions.insert(ext_props.extensionName);
        }
        return available_extensions;
    };

    FilterPhysicalDevicesResult result;

    for (auto const &physical_device : instance.raii().enumeratePhysicalDevices()) {
        auto const props = physical_device.getProperties();

        bool available{true};

        auto const available_extensions = get_available_extensions(physical_device);
        auto extension_supported = [&](Extension const &extension) {
            return available_extensions.contains(extension);
        };
        auto const features_chain = get_features_chain(physical_device);
        auto feature_supported = [&](Feature const &feature) {
            return feature_value(features_chain, feature) == vk::True;
        };

        if (requirements.vulkan_version > props.apiVersion) {
            available = false;
            result.device_unavailable_reasons[props.deviceName].push_back(std::format(
                "Vulkan {}.{}.{} required, but device supports only {}.{}.{}",
                vk::apiVersionMajor(requirements.vulkan_version),
                vk::apiVersionMinor(requirements.vulkan_version),
                vk::apiVersionPatch(requirements.vulkan_version),
                vk::apiVersionMajor(props.apiVersion), vk::apiVersionMinor(props.apiVersion),
                vk::apiVersionPatch(props.apiVersion)));
        }

        for (auto const &required_ext : requirements.extensions) {
            if (!extension_supported(required_ext)) {
                available = false;
                result.device_unavailable_reasons[props.deviceName].push_back(
                    std::format("Required extension {} is not supported", required_ext));
            }
        }

        for (auto const &required_feature : requirements.features) {
            if (!feature_supported(required_feature)) {
                available = false;
                result.device_unavailable_reasons[props.deviceName].push_back(std::format(
                    "Required feature {} is not supported", to_string(required_feature)));
            }
        }

        if (available)
            result.available_devices.push_back(physical_device);
    }

    return result;
}

} // namespace lc1
