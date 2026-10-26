#include "lc1/vk/core/device.hpp"

#include "lc1/vk/core/instance.hpp"
#include "lc1/vk/core/memory.hpp"

namespace lc1 {

Device::Device(Instance const &instance, vk::raii::PhysicalDevice physical,
               std::uint32_t queue_family, DeviceRequirements const &requirements)
    : physical_(std::move(physical)), queue_family_(queue_family), device_([&] {
          FeatureChain feature_chain{};
          for (auto const &feature : requirements.features) {
              feature_value(feature_chain, feature) = vk::True;
          }

          // The strings in requirements outlive this createDevice call.
          std::vector<char const *> extensions;
          extensions.reserve(requirements.extensions.size());
          for (auto const &extension : requirements.extensions) {
              extensions.push_back(extension.c_str());
          }

          vk::DeviceCreateInfo device_ci{
              .pNext = &feature_chain.get(),
          };
          vk::DeviceQueueCreateInfo queue_ci{
              .queueFamilyIndex = queue_family_,
          };
          constexpr auto queue_priority = 1.0F;
          queue_ci.setQueuePriorities(queue_priority);
          device_ci.setQueueCreateInfos(queue_ci);
          device_ci.setPEnabledExtensionNames(extensions);
          return physical_.createDevice(device_ci);
      }()),
      alloc_(instance.raii(), device_,
             vma::AllocatorCreateInfo{
                 .flags = vma::AllocatorCreateFlagBits::eBufferDeviceAddress |
                          vma::AllocatorCreateFlagBits::eKhrMaintenance5,
                 .physicalDevice = *physical_,
                 .vulkanApiVersion = requirements.vulkan_version,
             }),
      queue_(device_.getQueue(queue_family_, 0))
{
}

void Device::wait_idle() const
{
    try {
        device_.waitIdle();
    }
    catch (vk::SystemError const &error) {
        fail("vkDeviceWaitIdle failed: {}", error.what());
    }
}

vk::SampleCountFlagBits Device::max_sample_count() const
{
    auto physical_props = physical_.getProperties();
    vk::SampleCountFlags counts = physical_props.limits.framebufferColorSampleCounts &
                                  physical_props.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64)
        return vk::SampleCountFlagBits::e64;
    if (counts & vk::SampleCountFlagBits::e32)
        return vk::SampleCountFlagBits::e32;
    if (counts & vk::SampleCountFlagBits::e16)
        return vk::SampleCountFlagBits::e16;
    if (counts & vk::SampleCountFlagBits::e8)
        return vk::SampleCountFlagBits::e8;
    if (counts & vk::SampleCountFlagBits::e4)
        return vk::SampleCountFlagBits::e4;
    if (counts & vk::SampleCountFlagBits::e2)
        return vk::SampleCountFlagBits::e2;
    return vk::SampleCountFlagBits::e1;
}

} // namespace lc1

namespace lc1 {

std::uint32_t Device::queue_family() const
{
    return queue_family_;
}

vk::raii::PhysicalDevice const &Device::raii_physical() const
{
    return physical_;
}

vk::raii::Device const &Device::raii() const
{
    return device_;
}

MemoryAllocator const &Device::allocator() const
{
    return alloc_;
}

vk::raii::Queue const &Device::raii_queue() const
{
    return queue_;
}

} // namespace lc1
