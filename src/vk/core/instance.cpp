#include "lc1/vk/core/instance.hpp"

#include "lc1/vk/core/loader.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string_view>
#include <vector>

namespace lc1 {
namespace {

// The signature matches vk::PFN_DebugUtilsMessengerCallbackEXT so it can be
// handed to setPfnUserCallback without a cast -- that setter's C-typedef
// overload is deprecated in this header version.
VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                vk::DebugUtilsMessageTypeFlagsEXT /*types*/,
                                                vk::DebugUtilsMessengerCallbackDataEXT const *data,
                                                void * /*user_data*/)
{
    // hpp hands this over as the FlagBits enum even though the ABI is a
    // bitmask, so widen it before testing individual bits.
    auto const severity_flags = vk::DebugUtilsMessageSeverityFlagsEXT{severity};

    char const *label = "info";
    if (severity_flags & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        label = "ERROR";
    }
    else if (severity_flags & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        label = "warning";
    }

    // pMessageIdName carries the VUID -- by far the most useful field here.
    std::println(stderr, "[vk {}] {}\n    {}", label,
                 data->pMessageIdName != nullptr ? data->pMessageIdName : "(no id)",
                 data->pMessage != nullptr ? data->pMessage : "(no message)");

    if (severity_flags & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        // A validation error means we are already in undefined territory. Stop
        // at the first one rather than scrolling past hundreds of cascading
        // reports.
        std::fflush(stderr);
        std::abort();
    }
    return VK_FALSE;
}

} // namespace

Instance::Instance(VulkanLoader const &loader, std::vector<char const *> extensions,
                   std::vector<char const *> layers)
{
    try {
        vk::raii::Context const &context = loader.raii();

        if (context.enumerateInstanceVersion() < vk::ApiVersion14)
            fail("Vulkan loader version 1.4 or newer is required");

        auto const available_layers = context.enumerateInstanceLayerProperties();
        for (auto const *layer : layers) {
            if (std::ranges::none_of(available_layers, [layer](auto const &available_layer) {
                    return std::string_view{available_layer.layerName.data()} == layer;
                }))
                fail("required Vulkan layer '{}' is not available; install the layer and ensure "
                     "its manifest is discoverable through VK_LAYER_PATH (for Conan dependencies, "
                     "run with the generated conanrun environment)",
                     layer);
        }

        vk::InstanceCreateInfo info;

        constexpr vk::ApplicationInfo app_info{.pApplicationName = "lc1",
                                               .applicationVersion = vk::makeVersion(0, 1, 0),
                                               .pEngineName = "lc1",
                                               .engineVersion = vk::makeVersion(0, 1, 0),
                                               .apiVersion = vk::ApiVersion14};
        info.setPApplicationInfo(&app_info);

        auto const available = context.enumerateInstanceExtensionProperties();
        if (std::ranges::any_of(available, [](auto const &extension) {
                return std::string_view{extension.extensionName.data()} ==
                       vk::KHRPortabilityEnumerationExtensionName;
            })) {
            info.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
            if (std::ranges::none_of(extensions, [](auto const *extension) {
                    return std::string_view{extension} ==
                           vk::KHRPortabilityEnumerationExtensionName;
                }))
                extensions.push_back(vk::KHRPortabilityEnumerationExtensionName);
        }

        // Keep synchronization validation enabled whenever the validation layer is requested.
        // These values and the pNext chain remain alive through createInstance.
        vk::Bool32 const validate_sync = vk::True;
        vk::LayerSettingEXT const setting{
            .pLayerName = "VK_LAYER_KHRONOS_validation",
            .pSettingName = "validate_sync",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &validate_sync,
        };
        vk::LayerSettingsCreateInfoEXT layer_settings;
        if (std::ranges::any_of(layers, [](auto const *layer) {
                return std::string_view{layer} == "VK_LAYER_KHRONOS_validation";
            })) {
            if (std::ranges::none_of(extensions, [](auto const *extension) {
                    return std::string_view{extension} == vk::EXTLayerSettingsExtensionName;
                }))
                extensions.push_back(vk::EXTLayerSettingsExtensionName);
            layer_settings.setSettings(setting);
            info.setPNext(&layer_settings);
        }
        info.setPEnabledExtensionNames(extensions);
        info.setPEnabledLayerNames(layers);

        handle_ = context.createInstance(info);
    }
    catch (vk::SystemError const &error) {
        fail("Vulkan instance setup failed: {}", error.what());
    }
    catch (lc1::Error const &error) {
        throw Error(std::format("Vulkan instance setup failed: {}", error.what()),
                    error.location());
    }
}

void Instance::setup_debug_messenger()
{
    vk::DebugUtilsMessengerCreateInfoEXT messenger_info;
    // WARNING|ERROR only, not VERBOSE: sync validation is loud enough
    // already.
    messenger_info
        .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
        .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
        .setPfnUserCallback(debug_callback);
    messenger_ = vk::raii::DebugUtilsMessengerEXT(handle_, messenger_info);
}

} // namespace lc1

namespace lc1 {

vk::Instance Instance::handle() const
{
    return *handle_;
}

vk::raii::Instance const &Instance::raii() const
{
    return handle_;
}

} // namespace lc1
