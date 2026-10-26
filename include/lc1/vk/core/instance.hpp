#pragma once

#include "lc1/vk/core/common.hpp"
#include <vector>

namespace lc1 {

class VulkanLoader;

// VkInstance plus the validation debug messenger, created through the
// VulkanLoader's context; every raii dispatcher below descends from it.
class Instance {
  public:
    Instance(VulkanLoader const &loader, std::vector<char const *> extensions,
             std::vector<char const *> layers = {});

    // For glfwCreateWindowSurface, which takes a VkInstance.
    vk::Instance handle() const;

    // For Device: a vk::raii::SurfaceKHR cannot be built from a bare
    // VkInstance.
    vk::raii::Instance const &raii() const;

    void setup_debug_messenger();

  private:
    vk::raii::Instance handle_{nullptr};
    // Declared after handle_ so it is destroyed first.
    vk::raii::DebugUtilsMessengerEXT messenger_{nullptr};
};

} // namespace lc1
